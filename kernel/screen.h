#ifndef SCREEN_H
#define SCREEN_H

#define VIDEO_ADDRESS 0xb8000
#define MAX_ROWS 25
#define MAX_COLS 80

// Colors
#define WHITE_ON_BLACK 0x0f
#define RED_ON_BLACK 0x04
#define GREEN_ON_BLACK 0x02

void clear_vga_text();
void print_char(char character, char attribute);
void print_string(char* message);
void print_string_at(char* message, int col, int row);

// Redirect all print output to a sink (0 to restore normal console output)
void set_print_sink(void (*sink)(char));

#endif
