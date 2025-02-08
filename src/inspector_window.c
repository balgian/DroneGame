//
// Created by Gian Marco Balia
//
// src/inspector_window.c

#include <ncurses.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    initscr();
    cbreak();
    noecho();
    curs_set(FALSE);
    start_color();

    // Ottieni le dimensioni dello schermo
    int insp_height, insp_width;
    getmaxyx(stdscr, insp_height, insp_width);

    // Crea la finestra principale di inspection che occupa l'intero schermo
    WINDOW *inspect_win = newwin(insp_height, insp_width, 0, 0);
    box(inspect_win, 0, 0);
    wrefresh(inspect_win);

    // Assicurati che stdscr sia aggiornato
    refresh();

    // Calcola la metà della larghezza
    int half_width = insp_width / 2;

    // Crea le due finestre laterali usando newwin
    WINDOW *left_box = newwin(insp_height - 2, half_width - 2, 1, 1);
    WINDOW *right_box = newwin(insp_height - 2, insp_width - half_width - 2, 1, half_width + 1);

    // Disegna i bordi per ciascun box
    box(left_box, 0, 0);
    box(right_box, 0, 0);

    // Inserisci del testo nei box
    mvwprintw(left_box, 1, 1, "Box Sinistra");
    mvwprintw(left_box, 2, 1, "Info 1: ...");
    mvwprintw(left_box, 3, 1, "Info 2: ...");

    mvwprintw(right_box, 1, 1, "Box Destra");
    mvwprintw(right_box, 2, 1, "Dati: ...");
    mvwprintw(right_box, 3, 1, "Parametri: ...");

    // Aggiorna le finestre
    wrefresh(left_box);
    wrefresh(right_box);

    // Attendi un input per mantenere le finestre aperte
    getch();

    // Cleanup
    delwin(left_box);
    delwin(right_box);
    delwin(inspect_win);
    endwin();
    return EXIT_SUCCESS;
}