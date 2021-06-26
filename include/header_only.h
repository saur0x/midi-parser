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

static uint8_t midi_status;


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


/// Reverse the bytes of a 16 bit unsigned integer.
static inline uint16_t reverse16(uint16_t n)
{
	return (n << 8 & 0xFF00) | (n >> 8 & 0x00FF);
}

/// Reverse the bytes of a 32 bit unsigned integer.
static inline uint32_t reverse32(uint32_t n)
{
	return (n << 24 & 0xFF000000) | (n << 8 & 0x00FF0000) | (n >> 8 & 0x0000FF00) | (n >> 24 & 0x000000FF);
}

/// Read multi byte value from the MIDI file.
static uint32_t midi_value_read(FILE *midi)
{
	uint8_t buffer = 0x80;
	uint32_t value = 0;

	while (buffer & 0x80) {
		buffer = getc(midi);
		value = value << 7 | (buffer & 0x7F);
	}

	return value;
}

/// Read multi byte value from the MIDI file.
static uint32_t midi_value_peek(FILE *midi)
{
	uint8_t count = 0;
	uint8_t buffer = 0x80;
	uint32_t value = 0;

	while (buffer & 0x80) {
		buffer = getc(midi);
		value = value << 7 | (buffer & 0x7F);
		++count;
	}

	fseek(midi, -1 * count, SEEK_CUR);

	return value;
}


static struct midi_header *midi_header_new(struct midi_header *self, FILE *midi)
{
	uint16_t buffer16;
	uint32_t buffer32;

	fread(&buffer32, 4, 1, midi);
	if (buffer32 !=  * (uint32_t *) "MThd") {
		midi_status = MIDI_InvalidHeaderChunk;
		return NULL;
	}

	if (!self)
		self = (struct midi_header *) malloc(sizeof(struct midi_header));

	fread(&buffer32, 4, 1, midi);
	fread(&buffer16, 2, 1, midi);
	self->format = reverse16(buffer16);
	fread(&buffer16, 2, 1, midi);
	self->track_count = reverse16(buffer16);
	fread(&buffer16, 2, 1, midi);
	self->time_division = reverse16(buffer16);

	midi_status = MIDI_Success;
	return self;
}

static struct midi_event *midi_event_midi_new(struct midi_event *self, FILE *midi, uint8_t status)
{
	if (!self)
		self = (struct midi_event *) malloc(sizeof(struct midi_event));

	self->status = status;

	switch (status & 0xF0) {
	case EventNoteOff:
	case EventNoteOn:
	case EventKeyPressure:
	case EventControllerChange:
	case EventPitchBend:
		self->size = 2;
		break;
	case EventProgramChange:
	case EventChannelPressure:
		self->size = 1;
		break;
	default:
		midi_status = MIDI_NoCaseMatch;
		return NULL;
	}

	fread(self->midi_data, 1, self->size, midi);

	midi_status = MIDI_Success;
	return self;
}


static struct midi_event *midi_event_sysex_new(struct midi_event *self, FILE *midi)
{
	if (!self)
		self = (struct midi_event *) malloc(sizeof(struct midi_event));

	self->size = midi_value_read(midi);

#ifdef MIDI_SYSEX_EVENT
	// Read some bytes to the buffer and discard the rest of the bytes.
	size_t data_size = sizeof(self->sysex_data) / sizeof(uint8_t);
	size_t read_size = MIDI_MIN(self->size, data_size);

	fread(self->sysex_data, 1, read_size, midi);
	if (data_size < self->size)
		fseek(midi, self->size - data_size, SEEK_CUR);
#else
	fseek(midi, self->size, SEEK_CUR);
#endif

	midi_status = MIDI_Success;
	return self;
}

static struct midi_event *midi_event_meta_new(struct midi_event *self, FILE *midi)
{
	if (!self)
		self = (struct midi_event *) malloc(sizeof(struct midi_event));

	self->meta_type = MIDI_GETC(midi);
	self->size = midi_value_read(midi);

	switch (self->meta_type) {
	#if MIDI_META_EVENT >= 1
		case MetaEndOfTrack:
			break;
		case MetaSetTempo:
			self->meta_data.tempo = MIDI_GETC(midi) << 16 | MIDI_GETC(midi) << 8 | MIDI_GETC(midi);
			break;
	#endif
	#if MIDI_META_EVENT >= 2
		case MetaSequence:
			self->meta_data.sequence_number = MIDI_GETC(midi) << 8 | MIDI_GETC(midi);
			break;
		case MetaChannelPrefix:
			self->meta_data.channel_prefix = MIDI_GETC(midi);
			break;
		case MetaTimeSignature:
			fread(self->meta_data.time_signature, 1, 4, midi);
			break;
		case MetaKeySignature:
			fread(self->meta_data.key_signature, 1, 2, midi);
			break;
	#endif
	#if MIDI_META_EVENT >= 3
		case MetaSMPTEOffset:
			fread(self->meta_data.SMPTE_offset, 1, 5, midi);
			break;

		case MetaSequencerSpecific: {
			// Read some bytes to the buffer and discard rest of the bytes.
			size_t sequencer_specific_size = sizeof(self->meta_data.sequencer_specific) / sizeof(uint8_t);
			size_t read_size = MIDI_MIN(self->size, sequencer_specific_size);

			fread(self->meta_data.sequencer_specific, 1, read_size, midi);

			if (sequencer_specific_size < self->size)
				fseek(midi, self->size - sequencer_specific_size, SEEK_CUR);

			break;
		}

		case MetaText:
		case MetaCopyright:
		case MetaTrackName:
		case MetaInstrumentName:
		case MetaLyrics:
		case MetaMarker:
		case MetaCuePoint: {
			// Read some chars to the buffer and discard rest of the chars.
			size_t text_size = sizeof(self->meta_data.text) / sizeof(char) - 1;
			size_t read_size = MIDI_MIN(self->size, text_size);

			fread(self->meta_data.text, 1, read_size, midi);
			self->meta_data.text[read_size] = 0;

			if (text_size < self->size)
				fseek(midi, self->size - text_size, SEEK_CUR);

			puts(self->meta_data.text);
			break;
		}

	#endif

	default:
		fseek(midi, self->size, SEEK_CUR);
	}

	midi_status = MIDI_Success;
	return self;
}

struct midi_event *midi_event_new(struct midi_event *self, FILE *midi, uint8_t *running_status)
{
	if (!self)
		self = (struct midi_event *) malloc(sizeof(struct midi_event));

	self->dtime = midi_value_read(midi);
	self->status = MIDI_GETC(midi);

	// Handle MIDI running status
	if (self->status < 0x80) {
		self->status = *running_status;
		fseek(midi, -1, SEEK_CUR);
	}

	*running_status = self->status;

	switch (self->status & 0xF0) {
	case EventNoteOff:
	case EventNoteOn:
	case EventKeyPressure:
	case EventControllerChange:
	case EventPitchBend:
	case EventProgramChange:
	case EventChannelPressure:
		if (!midi_event_midi_new(self, midi, self->status))
			return NULL;
		break;

	case EventSystemExclusive:
		switch (self->status) {
		case 0xF0:
		case 0xF7:
			midi_event_sysex_new(self, midi);
			break;
		case 0xFF:
			midi_event_meta_new(self, midi);
		}
		break;

	default:
		midi_status = MIDI_NoCaseMatch;
		return NULL;
	}

	midi_status = MIDI_Success;
	return self;
}

struct midi_track *midi_track_new(struct midi_track *self, FILE *midi, size_t track_number)
{
	size_t saved_position = ftell(midi);
	uint32_t magic, track_size;

	fseek(midi, MIDI_HEADER_SIZE, SEEK_SET);

	// Skip previous tracks to get to the `track_number` track.
	for (size_t i = 0; i <= track_number; ++i) {
		fread(&magic, 1, 4, midi);
		assert(magic == * (uint32_t *) "MTrk");
		if (magic != * (uint32_t *) "MTrk") {
			midi_status = MIDI_InvalidTrackChunk;
			return NULL;
		}

		fread(&track_size, 4, 1, midi);
		track_size = reverse32(track_size);
		fseek(midi, track_size, SEEK_CUR);
	}

	if (!self)
		self = (struct midi_track *) malloc(sizeof(struct midi_track));

	self->start_position = ftell(midi) - track_size - MIDI_TRACK_HEADER_SIZE;
	self->current_position = self->start_position + MIDI_TRACK_HEADER_SIZE;
	self->size = track_size;
	self->running_status = 0;
	self->end_of_track = 0;

	fseek(midi, self->current_position, SEEK_SET);

	/// TODO: Improve peek
	self->next_event_timestamp = midi_value_peek(midi);

	// Default initial tempo is 120 BPM. Store it as micro seconds per quarter note.
	self->tempo = 60E6 / 120;

	fseek(midi, saved_position, SEEK_SET);
	midi_status = MIDI_Success;
	return self;
}

static inline uint8_t midi_track_over(struct midi_track *self)
{
	return self->end_of_track
	|| self->current_position >= self->start_position + MIDI_TRACK_HEADER_SIZE + self->size;
}


struct midi_event *midi_track_next(struct midi_track *self, FILE *midi, struct midi_event *event)
{
	if (!event)
		event = (struct midi_event *) malloc(sizeof(struct midi_event));

	size_t saved_position = ftell(midi);

	// Get `midi` to the `current_position` of this track.
	fseek(midi, self->current_position, SEEK_SET);

	midi_event_new(event, midi, &self->running_status);

	switch (event->status) {
	case 0xFF:
		switch (event->meta_type) {
		case MetaEndOfTrack:
			self->end_of_track = 1;
			break;
		case MetaSetTempo:
			self->tempo = event->meta_data.tempo;
		}
	}

	if (!midi_track_over(self))
		self->next_event_timestamp = midi_value_peek(midi);

	self->current_position = ftell(midi);

	fseek(midi, saved_position, SEEK_SET);
	midi_status = MIDI_Success;

	return event;
}



struct midi_parser *midi_parser_new(struct midi_parser *self, FILE *midi)
{
	assert(ftell(midi) == 0);

	struct midi_header header;
	if (!midi_header_new(&header, midi))
		return NULL;

	if (header.time_division >= 0x8000 || header.format >= 2) {
		midi_status = MIDI_Unimplemented;
		return NULL;
	}

	if (!self)
		self = (struct midi_parser *) calloc(1, sizeof(struct midi_parser));

	/// TODO: GET rid of below two lines.
	self->format = header.format;
	self->time_division = header.time_division;

	self->track_count = header.track_count;
	self->ticks_per_quarter = header.time_division & 0x7FFF;
	self->timestamp = 0;
	self->dtime = 0;

	self->active_track_count = self->track_count;

	#ifdef MIDI_TRACKS_ON_HEAP
		self->tracks = (struct midi_track *) malloc(sizeof(struct midi_track) * self->track_count);
	#endif

	// Load track into memory and the first event of that track in track's `event` buffer.
	for (size_t i = 0; i < self->track_count; ++i)
		midi_track_new(self->tracks + i, midi, i);

	return self;
}

#ifdef MIDI_TRACKS_ON_HEAP
static inline void midi_parser_free(struct midi_parser *self)
{
	if (self->tracks) {
		free(self->tracks);
		self->tracks = NULL;
	}
}
#endif

/**
Update parser state according to the event emitted.
*/
static void midi_parser_update(struct midi_parser *self, struct midi_event *event)
{
	switch (event->status & 0xF0) {
	case EventSystemExclusive:
		switch (event->status) {
		case 0xFF:
			switch (event->meta_type) {
			case MetaSetTempo:
				self->us_per_tick = event->meta_data.tempo / self->ticks_per_quarter;
				break;
			}
		}
	}
}


struct midi_event *midi_parser_next(struct midi_parser *self, FILE *midi, struct midi_event *event)
{
	if (self->end_of_file)
		return NULL;

	uint16_t active_track_count = 0;
	uint8_t track_over, chosen = 0;

	self->dtime = ~0;

	for (size_t i = 0; i < self->track_count; ++i) {
		struct midi_track *track = self->tracks + i;
		track_over = midi_track_over(track);

		if (track_over)
			continue;

		++active_track_count;

		assert(self->timestamp <= track->next_event_timestamp);

		if (!chosen && self->timestamp == track->next_event_timestamp) {
			// Get next event and update to absolute timestamp.
			event = midi_track_next(track, midi, event);
			track_over = midi_track_over(track);
			track->next_event_timestamp += self->timestamp;

			midi_parser_update(self, event);
			chosen = 1;
		}

		if (!track_over)
			self->dtime = MIDI_MIN(self->dtime, track->next_event_timestamp - self->timestamp);
	}

	self->end_of_file = !active_track_count;
	return event;
}


#endif /* MIDI_PARSER_H */