#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "midi_parser.h"


#define REAL_TIME
#define SHOW_KEYBOARD


void show_keyboard(uint8_t *notes, size_t size, FILE *output)
{
	for (size_t i = 21; i <= 108; ++i) {
		putc(notes[i] ? 'H' : '.', output);
	}
	putc('\n', output);
}

void parse(FILE *midi)
{
	struct midi_parser *parser = midi_parser_new(NULL, midi);
	struct midi_event event;
    uint8_t note, event_on, notes[128] = { 0 };
	FILE *data_stream = stdout;

	for (; !parser->end_of_file; parser->timestamp += parser->dtime) {
		midi_parser_next(parser, midi, &event);

		switch (MIDI_EVENT_TYPE(&event)) {
			case EventNoteOn: {
				note = event.midi_data[0];
				event_on = event.midi_data[1] != 0;
				notes[note] = event_on;
				break;
			}
			case EventNoteOff: {
				note = event.midi_data[0];
				event_on = 0;
				notes[note] = event_on;
				break;
			}
			case EventSystemExclusive: {
				switch (event.status) {
					case 0xF0:
					case 0xF7:
						fprintf(data_stream, "SYSEX:\t");
						for (size_t i = 0; i < event.size; ++i)
							fprintf(data_stream, "%hhu ", event.sysex_data[i]);
						break;
					case 0xFF:
						switch (event.meta_type) {
							case MetaText:
							case MetaCopyright:
							case MetaTrackName:
							case MetaInstrumentName:
							case MetaLyrics:
							case MetaMarker:
							case MetaCuePoint:
								fprintf(data_stream, "META:\t%s\n", event.meta_data.text);
								break;
						}
				}
			}
		}

        #ifdef SHOW_KEYBOARD
            show_keyboard(notes, 128, stderr);
        #endif
        #ifdef REAL_TIME
            usleep((useconds_t) MIDI_DELAY(parser));
        #endif
	}

	printf(
		"midi_header\tmidi_event\tmidi_track\tmidi_parser\n"
		"%lu bytes\t\t%lu bytes\t%lu bytes\t%lu bytes\n",
		sizeof(struct midi_header), sizeof(struct midi_event),
		sizeof(struct midi_track), sizeof(struct midi_parser)
	);

	free(parser);
}

int main(int argc, char **argv)
{
	FILE
	*midi = stdin,
	*output = stdout;

	switch (argc) {
	case 3:
		output = fopen(argv[2], "wb");
	case 2:
		midi = fopen(argv[1], "rb");
	}

	parse(midi);

	fclose(midi);
	fclose(output);

	return 0;
}