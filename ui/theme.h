/*
 * theme.h - EFTP's colours, EMail's way: a few, for meaning. With 256
 * colours EFTP sets its own soft shades in palette entries the desktop
 * doesn't use (and puts them back at the end); with 16 it takes the
 * standard ones; on monochrome and 4-colour screens nothing changes.
 */
#ifndef THEME_H
#define THEME_H

enum {
	P_TEXT,		/* ordinary text */
	P_DIM,		/* sizes, dates, column titles, notes */
	P_LINE,		/* rules and frames */
	P_PANEL,	/* light background: dividers, idle title bars, bar tracks */
	P_HEAD,		/* the focused pane's title bar */
	P_SEL,		/* the selection bar, progress */
	P_FOLDER,	/* folder icons: amber */
	P_UP,		/* going up to the server: terracotta, as the icon's arrow */
	P_DOWN,		/* coming down to the Atari: green, as the icon's arrow */
	P_ERR,		/* failed */
	NPEN
};

extern short pen[NPEN];
extern int colour;		/* colours are on (else black and white) */

void theme_init(void);		/* after the VDI is open and opt is read */
void theme_exit(void);		/* the palette back as it was */

#endif
