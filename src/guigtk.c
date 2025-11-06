#include <gtk/gtk.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#pragma pack(push, 1)
typedef struct {
  char chunkID[4];
  uint32_t chunkSize;
  char format[4];
} WavRiffHeader;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  char subchunkID[4];
  uint32_t subchunkSize;
} WavChunkHeader;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
} WavFmtData;
#pragma pack(pop)

typedef struct {
  GtkProgressBar *progress_bar;
  double fraction;
  gchar *text;
} ProgressData;

typedef struct {
  GtkLabel *status_label;
  gchar *text;
} StatusData;

typedef struct {
  gchar *input_filename;
  gchar *output_filename;
  gchar *operation;
  gint value;
  GtkProgressBar *progress_bar;
  GtkLabel *status_label;
  GtkWidget *process_button;
} ThreadData;

typedef struct {
  GtkWidget *window;
  GtkWidget *input_entry;
  GtkWidget *output_entry;
  GtkWidget *operation_combo;
  GtkWidget *value_spin;
  GtkWidget *value_label;
  GtkWidget *process_button;
  GtkWidget *progress_bar;
  GtkWidget *status_label;
  GtkWidget *file_info_label;
} AppWidgets;

void apply_right_shift(uint8_t *data, size_t size, int shift) {
  for (size_t i = 0; i < size; i++) {
    data[i] = (data[i] >> shift) & 0xFF;
  }
}

void apply_left_shift(uint8_t *data, size_t size, int shift) {
  for (size_t i = 0; i < size; i++) {
    data[i] = (data[i] << shift) & 0xFF;
  }
}

void apply_not(uint8_t *data, size_t size, int value) {
  for (size_t i = 0; i < size; i++) {
    data[i] = ~data[i] & 0xFF;
  }
}

void apply_and(uint8_t *data, size_t size, int value) {
  for (size_t i = 0; i < size; i++) {
    data[i] = (data[i] & value) & 0xFF;
  }
}

void apply_or(uint8_t *data, size_t size, int value) {
  for (size_t i = 0; i < size; i++) {
    data[i] = (data[i] | value) & 0xFF;
  }
}

void apply_xor(uint8_t *data, size_t size, int value) {
  for (size_t i = 0; i < size; i++) {
    data[i] = (data[i] ^ value) & 0xFF;
  }
}

int parse_wav_file(FILE *file, WavFmtData *fmtData, uint32_t *data_size,
                   long *data_offset) {
  WavRiffHeader riffHeader;

  if (fread(&riffHeader, sizeof(WavRiffHeader), 1, file) != 1) {
    return 0;
  }

  if (strncmp(riffHeader.chunkID, "RIFF", 4) != 0 ||
      strncmp(riffHeader.format, "WAVE", 4) != 0) {
    return 0;
  }

  WavChunkHeader chunkHeader;
  int fmt_found = 0;
  int data_found = 0;

  while (fread(&chunkHeader, sizeof(WavChunkHeader), 1, file) == 1) {
    if (strncmp(chunkHeader.subchunkID, "fmt ", 4) == 0) {
      if (fread(fmtData, sizeof(WavFmtData), 1, file) != 1) {
        return 0;
      }

      if (chunkHeader.subchunkSize > sizeof(WavFmtData)) {
        fseek(file, chunkHeader.subchunkSize - sizeof(WavFmtData), SEEK_CUR);
      }
      fmt_found = 1;
    } else if (strncmp(chunkHeader.subchunkID, "data", 4) == 0) {
      *data_size = chunkHeader.subchunkSize;
      *data_offset = ftell(file);
      data_found = 1;
      break;
    } else {
      fseek(file, chunkHeader.subchunkSize, SEEK_CUR);
    }
  }

  return (fmt_found && data_found);
}

gboolean update_progress_idle(gpointer data) {
  ProgressData *progress_data = (ProgressData *)data;
  gtk_progress_bar_set_fraction(progress_data->progress_bar,
                                progress_data->fraction);
  if (progress_data->text) {
    gtk_progress_bar_set_text(progress_data->progress_bar, progress_data->text);
  }
  g_free(progress_data->text);
  g_free(progress_data);
  return G_SOURCE_REMOVE;
}

gboolean update_status_idle(gpointer data) {
  StatusData *status_data = (StatusData *)data;
  gtk_label_set_text(status_data->status_label, status_data->text);
  g_free(status_data->text);
  g_free(status_data);
  return G_SOURCE_REMOVE;
}

gboolean enable_button_idle(gpointer data) {
  GtkWidget *button = (GtkWidget *)data;
  gtk_widget_set_sensitive(button, TRUE);
  return G_SOURCE_REMOVE;
}

gpointer process_wav_file_thread(gpointer data) {
  ThreadData *thread_data = (ThreadData *)data;

  FILE *input_file = fopen(thread_data->input_filename, "rb");
  if (!input_file) {
    StatusData *status_data = g_malloc(sizeof(StatusData));
    status_data->status_label = thread_data->status_label;
    status_data->text = g_strdup("Error: cannot open input file");
    g_idle_add(update_status_idle, status_data);
    g_idle_add(enable_button_idle, thread_data->process_button);
    g_free(thread_data->input_filename);
    g_free(thread_data->output_filename);
    g_free(thread_data->operation);
    g_free(thread_data);
    return GINT_TO_POINTER(FALSE);
  }

  WavFmtData fmtData;
  uint32_t data_size;
  long data_offset;

  if (!parse_wav_file(input_file, &fmtData, &data_size, &data_offset)) {
    StatusData *status_data = g_malloc(sizeof(StatusData));
    status_data->status_label = thread_data->status_label;
    status_data->text = g_strdup("Error: invalid WAV file format");
    g_idle_add(update_status_idle, status_data);
    g_idle_add(enable_button_idle, thread_data->process_button);
    fclose(input_file);
    g_free(thread_data->input_filename);
    g_free(thread_data->output_filename);
    g_free(thread_data->operation);
    g_free(thread_data);
    return GINT_TO_POINTER(FALSE);
  }

  if (fmtData.audioFormat != 1) {
    StatusData *status_data = g_malloc(sizeof(StatusData));
    status_data->status_label = thread_data->status_label;
    status_data->text = g_strdup("Error: only PCM format supported");
    g_idle_add(update_status_idle, status_data);
    g_idle_add(enable_button_idle, thread_data->process_button);
    fclose(input_file);
    g_free(thread_data->input_filename);
    g_free(thread_data->output_filename);
    g_free(thread_data->operation);
    g_free(thread_data);
    return GINT_TO_POINTER(FALSE);
  }

  if (fmtData.bitsPerSample != 8 && fmtData.bitsPerSample != 16) {
    StatusData *status_data = g_malloc(sizeof(StatusData));
    status_data->status_label = thread_data->status_label;
    status_data->text = g_strdup("Error: only 8-bit and 16-bit PCM supported");
    g_idle_add(update_status_idle, status_data);
    g_idle_add(enable_button_idle, thread_data->process_button);
    fclose(input_file);
    g_free(thread_data->input_filename);
    g_free(thread_data->output_filename);
    g_free(thread_data->operation);
    g_free(thread_data);
    return GINT_TO_POINTER(FALSE);
  }

  FILE *output_file = fopen(thread_data->output_filename, "wb");
  if (!output_file) {
    StatusData *status_data = g_malloc(sizeof(StatusData));
    status_data->status_label = thread_data->status_label;
    status_data->text = g_strdup("Error: cannot create output file");
    g_idle_add(update_status_idle, status_data);
    g_idle_add(enable_button_idle, thread_data->process_button);
    fclose(input_file);
    g_free(thread_data->input_filename);
    g_free(thread_data->output_filename);
    g_free(thread_data->operation);
    g_free(thread_data);
    return GINT_TO_POINTER(FALSE);
  }

  StatusData *status_data = g_malloc(sizeof(StatusData));
  status_data->status_label = thread_data->status_label;
  status_data->text = g_strdup("Processing audio data...");
  g_idle_add(update_status_idle, status_data);

  rewind(input_file);
  uint8_t *header_buffer = (uint8_t *)malloc(data_offset);
  if (!header_buffer) {
    StatusData *status_data = g_malloc(sizeof(StatusData));
    status_data->status_label = thread_data->status_label;
    status_data->text = g_strdup("Error: cannot allocate memory for header");
    g_idle_add(update_status_idle, status_data);
    g_idle_add(enable_button_idle, thread_data->process_button);
    fclose(input_file);
    fclose(output_file);
    g_free(thread_data->input_filename);
    g_free(thread_data->output_filename);
    g_free(thread_data->operation);
    g_free(thread_data);
    return GINT_TO_POINTER(FALSE);
  }

  if (fread(header_buffer, 1, data_offset, input_file) != data_offset) {
    StatusData *status_data = g_malloc(sizeof(StatusData));
    status_data->status_label = thread_data->status_label;
    status_data->text = g_strdup("Error: cannot read file header");
    g_idle_add(update_status_idle, status_data);
    g_idle_add(enable_button_idle, thread_data->process_button);
    free(header_buffer);
    fclose(input_file);
    fclose(output_file);
    g_free(thread_data->input_filename);
    g_free(thread_data->output_filename);
    g_free(thread_data->operation);
    g_free(thread_data);
    return GINT_TO_POINTER(FALSE);
  }

  if (fwrite(header_buffer, 1, data_offset, output_file) != data_offset) {
    StatusData *status_data = g_malloc(sizeof(StatusData));
    status_data->status_label = thread_data->status_label;
    status_data->text = g_strdup("Error: cannot write file header");
    g_idle_add(update_status_idle, status_data);
    g_idle_add(enable_button_idle, thread_data->process_button);
    free(header_buffer);
    fclose(input_file);
    fclose(output_file);
    g_free(thread_data->input_filename);
    g_free(thread_data->output_filename);
    g_free(thread_data->operation);
    g_free(thread_data);
    return GINT_TO_POINTER(FALSE);
  }

  free(header_buffer);

  const size_t BUFFER_SIZE = 1024 * 1024;
  uint8_t *buffer = (uint8_t *)malloc(BUFFER_SIZE);
  if (!buffer) {
    StatusData *status_data = g_malloc(sizeof(StatusData));
    status_data->status_label = thread_data->status_label;
    status_data->text = g_strdup("Error: cannot allocate buffer");
    g_idle_add(update_status_idle, status_data);
    g_idle_add(enable_button_idle, thread_data->process_button);
    fclose(input_file);
    fclose(output_file);
    g_free(thread_data->input_filename);
    g_free(thread_data->output_filename);
    g_free(thread_data->operation);
    g_free(thread_data);
    return GINT_TO_POINTER(FALSE);
  }

  size_t total_processed = 0;
  size_t bytes_remaining = data_size;

  while (bytes_remaining > 0) {
    size_t chunk_size =
        (bytes_remaining < BUFFER_SIZE) ? bytes_remaining : BUFFER_SIZE;

    size_t bytes_read = fread(buffer, 1, chunk_size, input_file);
    if (bytes_read != chunk_size) {
      StatusData *status_data = g_malloc(sizeof(StatusData));
      status_data->status_label = thread_data->status_label;
      status_data->text = g_strdup("Error: read incomplete chunk");
      g_idle_add(update_status_idle, status_data);
      g_idle_add(enable_button_idle, thread_data->process_button);
      free(buffer);
      fclose(input_file);
      fclose(output_file);
      g_free(thread_data->input_filename);
      g_free(thread_data->output_filename);
      g_free(thread_data->operation);
      g_free(thread_data);
      return GINT_TO_POINTER(FALSE);
    }

    if (strcmp(thread_data->operation, "right") == 0) {
      apply_right_shift(buffer, chunk_size, thread_data->value);
    } else if (strcmp(thread_data->operation, "left") == 0) {
      apply_left_shift(buffer, chunk_size, thread_data->value);
    } else if (strcmp(thread_data->operation, "not") == 0) {
      apply_not(buffer, chunk_size, thread_data->value);
    } else if (strcmp(thread_data->operation, "and") == 0) {
      apply_and(buffer, chunk_size, thread_data->value);
    } else if (strcmp(thread_data->operation, "or") == 0) {
      apply_or(buffer, chunk_size, thread_data->value);
    } else if (strcmp(thread_data->operation, "xor") == 0) {
      apply_xor(buffer, chunk_size, thread_data->value);
    }

    size_t bytes_written = fwrite(buffer, 1, chunk_size, output_file);
    if (bytes_written != chunk_size) {
      StatusData *status_data = g_malloc(sizeof(StatusData));
      status_data->status_label = thread_data->status_label;
      status_data->text = g_strdup("Error: write incomplete chunk");
      g_idle_add(update_status_idle, status_data);
      g_idle_add(enable_button_idle, thread_data->process_button);
      free(buffer);
      fclose(input_file);
      fclose(output_file);
      g_free(thread_data->input_filename);
      g_free(thread_data->output_filename);
      g_free(thread_data->operation);
      g_free(thread_data);
      return GINT_TO_POINTER(FALSE);
    }

    total_processed += chunk_size;
    bytes_remaining -= chunk_size;

    if (data_size > 0) {
      double progress = (double)total_processed / data_size;

      ProgressData *progress_data = g_malloc(sizeof(ProgressData));
      progress_data->progress_bar = thread_data->progress_bar;
      progress_data->fraction = progress;

      char progress_text[64];
      snprintf(progress_text, sizeof(progress_text),
               "Progress: %zu/%u bytes (%.1f%%)", total_processed, data_size,
               progress * 100);
      progress_data->text = g_strdup(progress_text);

      g_idle_add(update_progress_idle, progress_data);
    }
  }

  free(buffer);
  fclose(input_file);
  fclose(output_file);

  StatusData *final_status = g_malloc(sizeof(StatusData));
  final_status->status_label = thread_data->status_label;
  final_status->text = g_strdup("Processing completed successfully!");
  g_idle_add(update_status_idle, final_status);

  g_idle_add(enable_button_idle, thread_data->process_button);

  g_free(thread_data->input_filename);
  g_free(thread_data->output_filename);
  g_free(thread_data->operation);
  g_free(thread_data);

  return GINT_TO_POINTER(TRUE);
}

gboolean get_wav_file_info(const char *filename, char *info_text,
                           size_t info_size) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    snprintf(info_text, info_size, "Error: cannot open file");
    return FALSE;
  }

  WavFmtData fmtData;
  uint32_t data_size;
  long data_offset;

  if (!parse_wav_file(file, &fmtData, &data_size, &data_offset)) {
    snprintf(info_text, info_size, "Error: invalid WAV file");
    fclose(file);
    return FALSE;
  }

  fclose(file);

  snprintf(info_text, info_size,
           "Channels: %d\nSample rate: %d Hz\nBits per sample: %d\nData size: "
           "%u bytes",
           fmtData.numChannels, fmtData.sampleRate, fmtData.bitsPerSample,
           data_size);

  return TRUE;
}

void on_operation_changed(GtkComboBox *combo, AppWidgets *widgets) {
  gchar *operation = gtk_combo_box_text_get_active_text(
      GTK_COMBO_BOX_TEXT(widgets->operation_combo));

  if (g_strcmp0(operation, "not") == 0) {
    gtk_widget_set_sensitive(widgets->value_spin, FALSE);
    gtk_label_set_text(GTK_LABEL(widgets->value_label), "Value (ignored):");
  } else if (g_strcmp0(operation, "right") == 0 ||
             g_strcmp0(operation, "left") == 0) {
    gtk_widget_set_sensitive(widgets->value_spin, TRUE);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(widgets->value_spin), 0, 7);
    gtk_label_set_text(GTK_LABEL(widgets->value_label), "Shift value (0-7):");
  } else {
    gtk_widget_set_sensitive(widgets->value_spin, TRUE);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(widgets->value_spin), 0, 255);
    gtk_label_set_text(GTK_LABEL(widgets->value_label), "Value (0-255):");
  }

  g_free(operation);
}

void on_input_file_changed(GtkEntry *entry, AppWidgets *widgets) {
  const gchar *filename = gtk_entry_get_text(entry);

  if (g_strcmp0(filename, "") != 0) {
    char info_text[256];
    if (get_wav_file_info(filename, info_text, sizeof(info_text))) {
      gtk_label_set_text(GTK_LABEL(widgets->file_info_label), info_text);
    } else {
      gtk_label_set_text(GTK_LABEL(widgets->file_info_label), info_text);
    }
  } else {
    gtk_label_set_text(GTK_LABEL(widgets->file_info_label), "No file selected");
  }
}

void on_browse_input_clicked(GtkButton *button, AppWidgets *widgets) {
  GtkWidget *dialog = gtk_file_chooser_dialog_new(
      "Open WAV File", GTK_WINDOW(widgets->window),
      GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open",
      GTK_RESPONSE_ACCEPT, NULL);

  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "WAV Files (*.wav)");
  gtk_file_filter_add_pattern(filter, "*.wav");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_entry_set_text(GTK_ENTRY(widgets->input_entry), filename);
    g_free(filename);
  }

  gtk_widget_destroy(dialog);
}

void on_browse_output_clicked(GtkButton *button, AppWidgets *widgets) {
  GtkWidget *dialog = gtk_file_chooser_dialog_new(
      "Save WAV File", GTK_WINDOW(widgets->window),
      GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save",
      GTK_RESPONSE_ACCEPT, NULL);

  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "WAV Files (*.wav)");
  gtk_file_filter_add_pattern(filter, "*.wav");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
  gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),
                                                 TRUE);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_entry_set_text(GTK_ENTRY(widgets->output_entry), filename);
    g_free(filename);
  }

  gtk_widget_destroy(dialog);
}

void on_process_clicked(GtkButton *button, AppWidgets *widgets) {
  const gchar *input_file = gtk_entry_get_text(GTK_ENTRY(widgets->input_entry));
  const gchar *output_file =
      gtk_entry_get_text(GTK_ENTRY(widgets->output_entry));

  if (g_strcmp0(input_file, "") == 0 || g_strcmp0(output_file, "") == 0) {
    gtk_label_set_text(GTK_LABEL(widgets->status_label),
                       "Error: please select input and output files");
    return;
  }

  gchar *operation = gtk_combo_box_text_get_active_text(
      GTK_COMBO_BOX_TEXT(widgets->operation_combo));
  gint value =
      gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->value_spin));

  gtk_widget_set_sensitive(widgets->process_button, FALSE);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(widgets->progress_bar), 0.0);
  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(widgets->progress_bar),
                            "Starting...");
  gtk_label_set_text(GTK_LABEL(widgets->status_label), "Processing...");

  ThreadData *thread_data = g_malloc(sizeof(ThreadData));
  thread_data->input_filename = g_strdup(input_file);
  thread_data->output_filename = g_strdup(output_file);
  thread_data->operation = g_strdup(operation);
  thread_data->value = value;
  thread_data->progress_bar = GTK_PROGRESS_BAR(widgets->progress_bar);
  thread_data->status_label = GTK_LABEL(widgets->status_label);
  thread_data->process_button = widgets->process_button;

  g_thread_new("process_thread", process_wav_file_thread, thread_data);

  g_free(operation);
}

AppWidgets *create_gui() {
  AppWidgets *widgets = g_malloc(sizeof(AppWidgets));

  widgets->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(widgets->window),
                       "WAV File Bitwise Operations");
  gtk_window_set_default_size(GTK_WINDOW(widgets->window), 500, 400);
  gtk_container_set_border_width(GTK_CONTAINER(widgets->window), 10);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_container_add(GTK_CONTAINER(widgets->window), grid);

  GtkWidget *input_label = gtk_label_new("Input WAV File:");
  gtk_widget_set_halign(input_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), input_label, 0, 0, 1, 1);

  widgets->input_entry = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid), widgets->input_entry, 1, 0, 2, 1);

  GtkWidget *input_button = gtk_button_new_with_label("Browse...");
  gtk_grid_attach(GTK_GRID(grid), input_button, 3, 0, 1, 1);

  GtkWidget *output_label = gtk_label_new("Output WAV File:");
  gtk_widget_set_halign(output_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), output_label, 0, 1, 1, 1);

  widgets->output_entry = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid), widgets->output_entry, 1, 1, 2, 1);

  GtkWidget *output_button = gtk_button_new_with_label("Browse...");
  gtk_grid_attach(GTK_GRID(grid), output_button, 3, 1, 1, 1);

  GtkWidget *info_label = gtk_label_new("File Info:");
  gtk_widget_set_halign(info_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), info_label, 0, 2, 1, 1);

  widgets->file_info_label = gtk_label_new("No file selected");
  gtk_label_set_line_wrap(GTK_LABEL(widgets->file_info_label), TRUE);
  gtk_label_set_xalign(GTK_LABEL(widgets->file_info_label), 0.0);
  gtk_widget_set_halign(widgets->file_info_label, GTK_ALIGN_START);
  GtkWidget *info_frame = gtk_frame_new(NULL);
  gtk_container_add(GTK_CONTAINER(info_frame), widgets->file_info_label);
  gtk_grid_attach(GTK_GRID(grid), info_frame, 1, 2, 3, 1);

  GtkWidget *operation_label = gtk_label_new("Operation:");
  gtk_widget_set_halign(operation_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), operation_label, 0, 3, 1, 1);

  widgets->operation_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->operation_combo),
                                 "right");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->operation_combo),
                                 "left");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->operation_combo),
                                 "not");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->operation_combo),
                                 "and");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->operation_combo),
                                 "or");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->operation_combo),
                                 "xor");
  gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->operation_combo), 0);
  gtk_grid_attach(GTK_GRID(grid), widgets->operation_combo, 1, 3, 1, 1);

  widgets->value_label = gtk_label_new("Shift value (0-7):");
  gtk_widget_set_halign(widgets->value_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), widgets->value_label, 2, 3, 1, 1);

  widgets->value_spin = gtk_spin_button_new_with_range(0, 7, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->value_spin), 1);
  gtk_grid_attach(GTK_GRID(grid), widgets->value_spin, 3, 3, 1, 1);

  widgets->process_button = gtk_button_new_with_label("Process WAV File");
  gtk_widget_set_halign(widgets->process_button, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), widgets->process_button, 0, 4, 4, 1);

  widgets->progress_bar = gtk_progress_bar_new();
  gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(widgets->progress_bar), TRUE);
  gtk_grid_attach(GTK_GRID(grid), widgets->progress_bar, 0, 5, 4, 1);

  widgets->status_label = gtk_label_new("Ready");
  gtk_widget_set_halign(widgets->status_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), widgets->status_label, 0, 6, 4, 1);

  g_signal_connect(widgets->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(widgets->operation_combo, "changed",
                   G_CALLBACK(on_operation_changed), widgets);
  g_signal_connect(input_button, "clicked", G_CALLBACK(on_browse_input_clicked),
                   widgets);
  g_signal_connect(output_button, "clicked",
                   G_CALLBACK(on_browse_output_clicked), widgets);
  g_signal_connect(widgets->process_button, "clicked",
                   G_CALLBACK(on_process_clicked), widgets);
  g_signal_connect(widgets->input_entry, "changed",
                   G_CALLBACK(on_input_file_changed), widgets);

  return widgets;
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  gtk_init(&argc, &argv);

  AppWidgets *widgets = create_gui();

  gtk_widget_show_all(widgets->window);

  gtk_main();

  g_free(widgets);

  return 0;
}
