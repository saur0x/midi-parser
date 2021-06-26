#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>


#define MIDI_META_EVENT 1
#undef MIDI_SYSEX_EVENT

#define MIDI_GETC getc
#undef MIDI_TRACKS_ON_HEAP

#define MIDI_HEADER_SIZE 14
#define MIDI_TRACK_HEADER_SIZE 8

/// Return minimum of two 32 bit unsigned integers.
#define MIDI_MIN(x, y) ((x) <= (y) ? (x) : (y))

/// Return maximum of two 32 bit unsigned integers.
#define MIDI_MAX(x, y) ((x) >= (y) ? (x) : (y))

#define MIDI_EVENT_TYPE(midi_event) ((midi_event)->status & 0xF0)
#define MIDI_EVENT_CHANNEL(midi_event) ((midi_event)->status & 0x0F)

#define MIDI_DELAY(midi_parser) (((midi_parser)->dtime * (midi_parser)->us_per_tick))


extern uint8_t midi_status;


enum MIDI_EventType
{
	EventNoteOff = 0x80,
	EventNoteOn = 0x90,
	EventKeyPressure = 0xA0,
	EventControllerChange = 0xB0,
	EventProgramChange = 0xC0,
	EventChannelPressure = 0xD0,
	EventPitchBend = 0xE0,
	EventSystemExclusive = 0xF0
};

enum MIDI_MetaEventType
{
	MetaSequence = 0x00,
	MetaText = 0x01,
	MetaCopyright = 0x02,
	MetaTrackName = 0x03,
	MetaInstrumentName = 0x04,
	MetaLyrics = 0x05,
	MetaMarker = 0x06,
	MetaCuePoint = 0x07,
	MetaChannelPrefix = 0x20,
	MetaEndOfTrack = 0x2F,
	MetaSetTempo = 0x51,
	MetaSMPTEOffset = 0x54,
	MetaTimeSignature = 0x58,
	MetaKeySignature = 0x59,
	MetaSequencerSpecific = 0x7F
};

enum
{
	MIDI_Success,
	MIDI_InvalidHeaderChunk,
	MIDI_InvalidTrackChunk,
	MIDI_PotentialBufferOverflow,
	MIDI_NoCaseMatch,
	MIDI_Unimplemented
};


struct midi_header
{
	uint16_t format, track_count, time_division;
};


struct midi_event
{
	uint32_t dtime;
	uint8_t status;
	uint32_t size;

	union
	{
		uint8_t midi_data[2];

		#ifdef MIDI_SYSEX_EVENT
			uint8_t sysex_data[128];
		#endif

		struct
		{
			uint8_t meta_type;
			union
			{
				#if MIDI_META_EVENT >= 1
					uint32_t tempo;
				#endif

				#if MIDI_META_EVENT >= 2
					uint8_t channel_prefix;
					uint16_t sequence_number;
					uint8_t key_signature[2];
					uint8_t time_signature[4];
				#endif

				#if MIDI_META_EVENT >= 3
					uint8_t SMPTE_offset[5];
					uint8_t sequencer_specific[128];
					char text[128];
				#endif
			}
			meta_data;
		};
	};
};


struct midi_track
{
	uint32_t start_position;
	uint32_t current_position;
	uint32_t size;

	// In micro seconds per quarter note
	uint32_t tempo;
	uint8_t end_of_track;

	uint8_t running_status;
	uint32_t next_event_timestamp;
};


struct midi_parser
{
	uint16_t format, track_count, time_division, active_track_count;

	uint32_t
	ticks_per_quarter,
	us_per_tick,
	timestamp,
	dtime;

	uint8_t end_of_file;

	#ifdef MIDI_TRACKS_ON_HEAP
		struct midi_track *tracks;
	#else
		struct midi_track tracks[16];
	#endif
};


struct midi_event *midi_event_new(struct midi_event *self, FILE *midi, uint8_t *running_status);

struct midi_track *midi_track_new(struct midi_track *self, FILE *midi, size_t track_number);

struct midi_parser *midi_parser_new(struct midi_parser *self, FILE *midi);

struct midi_event *midi_track_next(struct midi_track *self, FILE *midi, struct midi_event *event);

struct midi_event *midi_parser_next(struct midi_parser *self, FILE *midi, struct midi_event *event);


#endif /* MIDI_PARSER_H */