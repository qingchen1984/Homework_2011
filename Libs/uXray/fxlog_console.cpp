#include "stdafx.h"
#include "fxlog_console.h"

// -----------------------------------------------------------------------------
// Library		FXlib
// File			fxlog_console.cpp
// Author		intelfx
// Description	Console (stdio) logger backend for debug system
// -----------------------------------------------------------------------------

ImplementDescriptor(FXConLog, "console logger backend", MOD_INTERNAL, FXConLog);

FILE* const FXConLog::FALLBACK_FILE = stderr;

// -----------------------------------------------------------------------------
// Implementation
// -----------------------------------------------------------------------------

FXConLog::FXConLog (bool UseANSIEscapeSequences) :
output_mutex (PTHREAD_MUTEX_INITIALIZER),
extended_mode (UseANSIEscapeSequences ? EXT_ANSI : EXT_NONE)
{
	pthread_mutex_init (&output_mutex, 0);
}

FXConLog::FXConLog() :
output_mutex (PTHREAD_MUTEX_INITIALIZER),
extended_mode (EXT_NONE)
{
#ifdef TARGET_POSIX
	extended_mode = EXT_ANSI;
#else
	extended_mode = EXT_NONE;
	fprintf (stderr, "Microsoft are stupid motherfuckers... They've DROPPED the ANSI codes support!\n");
#endif

	pthread_mutex_init (&output_mutex, 0);
}

FXConLog::~FXConLog()
{
	pthread_mutex_destroy (&output_mutex);
}

// -----------------------------------------------------------------------------
// Target management
// -----------------------------------------------------------------------------

void FXConLog::RegisterTarget (Debug::TargetDescriptor_* target)
{
	bool ready_for_init = !Debug::TargetIsOK (target);
	if (!ready_for_init) return;


	const char* filename = target ->target_name;
	void* descriptor = 0;

	// Try to open the file, if name string looks good.
	if (filename && filename[0])
		descriptor = OpenFile (filename);

	// If file is not opened for some reason, fall back to static file descriptor.
	if (!descriptor)
		descriptor = FALLBACK_FILE;

	target ->target_descriptor	= descriptor;
	target ->target_engine		= this;
}

void FXConLog::CloseTarget (Debug::TargetDescriptor_* target)
{
	bool ready_for_disown = Debug::TargetIsOK (target) &&
							(target ->target_engine == this);
	if (!ready_for_disown) return;


	FILE* descriptor = reinterpret_cast<FILE*> (target ->target_descriptor);

	if (descriptor != FALLBACK_FILE)
		CloseFile (descriptor);
}

// -----------------------------------------------------------------------------
// Descriptor management
// -----------------------------------------------------------------------------

FILE* FXConLog::OpenFile (const char* filename)
{
	// We'll handle "stdout" and "stderr" as user hopes
	if (!strcmp (filename, "stderr"))
		return stderr;

	if (!strcmp (filename, "stdout"))
		return stdout;

	return fopen (filename, "a");
}

void FXConLog::CloseFile (FILE* descriptor)
{
	if (descriptor == stdout)
		return;

	if (descriptor == stderr)
		return;

	fclose (descriptor);
}

// -----------------------------------------------------------------------------
// Console management
// -----------------------------------------------------------------------------

void FXConLog::SetExtended (FILE* stream, FXConLog::ConsoleExtendedData data)
{
	switch (extended_mode)
	{
	case EXT_ANSI:
		// ECMA-48 (ANSI X3.64) SGR sequence
		fprintf (stream, "\033[0%s", data.brightness ? ";1" : "");

		if (data.foreground != CCC_TRANSPARENT)
			fprintf (stream, ";%d", 30 + data.foreground);

		if (data.background != CCC_TRANSPARENT)
			fprintf (stream, ";%d", 40 + data.background);

		fputc ('m', stream);
		break;

	case EXT_NONE:
	default:
		break;
	}
}

void FXConLog::ResetExtended (FILE* stream)
{
	switch (extended_mode)
	{
	case EXT_ANSI:
		fprintf (stream, "\033[m");
		break;

	case EXT_NONE:
	default:
		break;
	}
}

void FXConLog::ClearLine (FILE* stream)
{
	switch (extended_mode)
	{
		case EXT_ANSI:
			// ECMA-48 (ANSI X.364) CSI EL sequence
			fprintf (stream, "\033[K");
			break;

		case EXT_NONE:
		default:
			break;
	}
}


void FXConLog::SetPosition (FILE* stream, unsigned short column, unsigned short row /* =0 */)
{
	switch (extended_mode)
	{
	case EXT_ANSI:
		// ECMA-48 (ANSI X3.64) CSI CHA sequence
		if (column)
			fprintf (stream, "\033[%dG", column);

		// ECMA-48 (ANSI X3.64) CSI VPA sequence
		if (row)
			fprintf (stream, "\033[%dd", row);

		break;

	default:
	case EXT_NONE:
		break;
	}
}


// -----------------------------------------------------------------------------
// Internal write procedures
// -----------------------------------------------------------------------------

void FXConLog::InternalWrite (Debug::EventDescriptor event,
                              Debug::SourceDescriptor place,
							  Debug::ObjectParameters object,
                              FILE* target)
{
	static const unsigned short MSG_BASE_COLUMN = 60;

	// The look of what shall we print is dynamic.
	ConsoleExtendedData_ data;
	ConsoleExtendedData_ secondary_data;
	ConsoleExtendedData_ status_data;

	data.background = CCC_TRANSPARENT;
	data.foreground = CCC_WHITE;
	data.brightness = 0;

	secondary_data.background = CCC_TRANSPARENT;
	secondary_data.foreground = CCC_WHITE;
	secondary_data.brightness = 1;

	// Step 1. Set the text attributes and write the specificator. All according to event type.
	const char* msgspec = 0;

	switch (event.event_type)
	{
	case EventTypeIndex_::E_INFO:
		data.foreground = CCC_GREEN;
		data.brightness = 1;

		switch (event.event_level)
		{
		default:
		case EventLevelIndex_::E_USER:
			msgspec = "INFO\t";
			data.brightness = 0;
			break;

		case EventLevelIndex_::E_VERBOSE:
			msgspec = "VERBOSE\t";
			break;

		case EventLevelIndex_::E_DEBUG:
			msgspec = "DEBUG\t";
			break;
		}

		break;

	case EventTypeIndex_::E_WARNING:
		data.foreground = CCC_YELLOW;
		data.brightness = 1;
		msgspec = "WARNING\t";
		break;

	case EventTypeIndex_::E_CRITICAL:
		data.foreground = CCC_RED;
		data.brightness = 0;
		msgspec = "ERROR\t";
		break;

	case EventTypeIndex_::E_OBJCREATION:
		data.foreground = CCC_BLUE;
		data.brightness = 0;
		msgspec = "-- CTOR\t";
		break;

	case EventTypeIndex_::E_OBJDESTRUCTION:
		data.foreground = CCC_RED;
		data.brightness = 0;
		msgspec = "-- DTOR\t";
		break;

	case EventTypeIndex_::E_EXCEPTION:
		data.foreground = CCC_WHITE;
		data.brightness = 1;
		msgspec = "EXCEPT\t";
		break;

	case EventTypeIndex_::E_FUCKING_EPIC_SHIT:
		data.foreground = CCC_WHITE;
		data.brightness = 1;
		msgspec = "-- EPIC SHIT ";
		break;

	default:
		data.foreground = CCC_WHITE;
		data.brightness = 1;
		msgspec = "UNKNOWN TYPE MESSAGE ";
	}

	SetExtended (target, data);
	fprintf (target, msgspec);

	// Step 2. Write position (place)
	// It comes in two flavors - high-level (module name) and low-level (file-function-line).
	if ( object.object_status != Debug::OS_BAD &&
		 (event.event_level == EventLevelIndex_::E_USER ||
		  event.event_level == EventLevelIndex_::E_VERBOSE ||
	      event.event_level == EventLevelIndex_::E_VERBOSE ||
	      event.event_type == EventTypeIndex_::E_OBJCREATION ||
	      event.event_type == EventTypeIndex_::E_OBJDESTRUCTION) &&
		 (event.event_type != EventTypeIndex_::E_EXCEPTION) &&
	     (event.event_type != EventTypeIndex_::E_FUCKING_EPIC_SHIT))
	{
		fprintf (target, "in %s%s ",
		         object.object_descriptor ->object_type == ModuleType_::MOD_APPMODULE ? "" : "object ",
		         object.object_descriptor ->object_name);

		// Set next color
		SetExtended (target, secondary_data);
	}

	else
	{
		// Some build systems tend to feed compiler with full pathnames. That messes up the log output.
		// We can't tell if slashes in path are relative or absolute, so at least cut them all.
		const char* adaptive_source_name = strrchr (place.source_name, '/');
		if (!adaptive_source_name) adaptive_source_name = strrchr (place.source_name, '\\');
		if (!adaptive_source_name) adaptive_source_name = place.source_name; // In case of no slashes
		else ++adaptive_source_name; // adaptive_source_name points to last slash, increment it


		fprintf (target, "in function %s ", place.function);

		SetExtended (target, secondary_data);
		fprintf (target, "(%s:%d) ", adaptive_source_name, place.source_line);
	}

	// Step 3. Move to column (color is set at Step 2)
	SetPosition (target, MSG_BASE_COLUMN);
	ClearLine (target); // we do not limit length of file
	fprintf (target, ":: ");

	// Step 4. Write object state.
	if ((event.event_type == Debug::E_OBJCREATION))
	{
		status_data = data;
		msgspec = "CREATE\t";
	}

	else if ((event.event_type == Debug::E_OBJDESTRUCTION))
	{
		status_data = data;
		msgspec = "DELETE\t";
	}

	else if (object.object_descriptor ->object_id == Debug::GLOBAL_OID)
	{
		status_data.background = CCC_TRANSPARENT;
		status_data.foreground = CCC_WHITE;
		status_data.brightness = 0;
		msgspec = "GLOBAL\t";
	}

	else switch (object.object_status)
	{
		default:
		case Debug::OS_UNCHECKED:
			status_data.background = CCC_TRANSPARENT;
			status_data.foreground = CCC_WHITE;
			status_data.brightness = 0;
			msgspec = "UNKNOWN\t";
			break;

		case Debug::OS_MOVED:
			status_data.background = CCC_TRANSPARENT;
			status_data.foreground = CCC_WHITE;
			status_data.brightness = 0;
			msgspec = "MOVED\t";
			break;

		case Debug::OS_OK:
			status_data.background = CCC_TRANSPARENT;
			status_data.foreground = CCC_GREEN;
			status_data.brightness = 0;
			msgspec = "OK\t";
			break;

		case Debug::OS_BAD:
			status_data.background = CCC_TRANSPARENT;
			status_data.foreground = CCC_RED;
			status_data.brightness = 1;
			msgspec = "BAD\t";
			break;
	}

	SetExtended (target, status_data);
	fprintf (target, msgspec);

	SetExtended (target, secondary_data);
	fprintf (target, ":: ");

	// Step 4. Write message.
	SetExtended (target, data);
	// FUCK MY BRAIN!!! @ 09.08.11 18:03 GMT+0300
	/*fprintf*/ vfprintf (target, event.message_format_string, event.message_args);

	// Step 4. Reset extended attributes and do a newline.
	ResetExtended (target);
	fputc ('\n', target);

	if (event.event_type != EventTypeIndex_::E_INFO)
	{
		fflush (target);
	}
}


void FXConLog::WriteLog (Debug::LogAtom atom)
{
	// Cache (alias) useful parts of the atom.
	Debug::EventDescriptor	event = atom.event;
	Debug::SourceDescriptor	place = atom.place;
	Debug::ObjectParameters	object = atom.object;
	FILE*					target = reinterpret_cast<FILE*> (atom.target.target_descriptor);

	// Remember we can throw exceptions here? ;)
	__assert (target, "Invalid stream");
	__assert (!ferror (target), "Stream has an error");

	pthread_mutex_lock (&output_mutex);
	InternalWrite (event, place, object, target);
	pthread_mutex_unlock (&output_mutex);
}

void FXConLog::WriteLogEmergency (Debug::LogAtom atom) throw()
{
	// Cache (alias) useful parts of the atom.
	Debug::EventDescriptor	event = atom.event;
	Debug::SourceDescriptor	place = atom.place;
	Debug::ObjectParameters	object = atom.object;
	FILE*					target = reinterpret_cast<FILE*> (atom.target.target_descriptor);

	bool stream_error = 0;
	target || (stream_error = 1, target = stderr);
	ferror (target) && (stream_error = 1, target = stderr);

	ConsoleExtendedData_ emerg_data;
	emerg_data.background = CCC_BLACK;
	emerg_data.foreground = CCC_RED;
	emerg_data.brightness = 1;

	pthread_mutex_lock (&output_mutex);
	SetExtended (target, emerg_data);
	fprintf (target, "(EMERG)%s ", stream_error ? " (STREAM FAULT)" : "");

	InternalWrite (event, place, object, target);
	pthread_mutex_unlock (&output_mutex);
}

// kate: indent-mode cstyle; replace-tabs off; tab-width 4;
