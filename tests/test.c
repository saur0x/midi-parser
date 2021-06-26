#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "midi_parser.h"


void show_keyboard(uint8_t *notes, size_t size, FILE *output)
{
    for (size_t i = 21; i <= 108; ++i) {
        putc(notes[i] ? 'H' : '.', output);
    }
    putc('\n', output);
}

void parse_midi(struct midi_parser *parser, FILE *midi)
{
	struct midi_event event;

    uint8_t notes[128] = { 0 };
	uint8_t note, event_on;

	while (!parser->end_of_file) {
		midi_parser_next(parser, midi, &event);

		// switch (event.status & 0xF0) {
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
		}

		show_keyboard(notes, 128, stderr);
		// usleep((useconds_t) MIDI_DELAY(parser));
		parser->timestamp += parser->dtime;
	}

	printf(
		"midi_header\tmidi_event\tmidi_track\tmidi_parser\n"
		"%lu\t%lu\t%lu\t%lu\n",
		sizeof(struct midi_header), sizeof(struct midi_event),
		sizeof(struct midi_track), sizeof(struct midi_parser)
	);
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

	struct midi_parser *parser = midi_parser_new(NULL, midi);

	parse_midi(parser, midi);

	free(parser);

    fclose(midi);
    fclose(output);

	return 0;
}