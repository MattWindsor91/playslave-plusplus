// This file is part of playd.
// playd is licensed under the MIT licence: see LICENSE.txt.

/**
 * @file
 * Implementation of the PipeAudio class.
 * @see audio/pipe_audio.hpp
 */

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <string>

#include "../errors.hpp"
#include "../messages.h"
#include "../response.hpp"
#include "audio.hpp"
#include "audio_sink.hpp"
#include "audio_source.hpp"
#include "sample_formats.hpp"

//
// Audio
//

std::unique_ptr<Response> Audio::Emit(const std::string &, bool)
{
	// By default, emit nothing.  This is an acceptable behaviour.
	return std::unique_ptr<Response>();
}

//
// NoAudio
//

Audio::State NoAudio::Update()
{
	return Audio::State::NONE;
}

std::unique_ptr<Response> NoAudio::Emit(const std::string &path, bool)
{
	std::unique_ptr<Response> ret;

	if (path == "/control/state") {
		ret = std::move(
			Response::Res("Entry", "/control/state", "Ejected")
		);
	}

	return ret;
}

void NoAudio::SetPlaying(bool)
{
	throw NoAudioError(MSG_CMD_NEEDS_LOADED);
}

void NoAudio::Seek(std::uint64_t)
{
	throw NoAudioError(MSG_CMD_NEEDS_LOADED);
}

std::uint64_t NoAudio::Position() const
{
	throw NoAudioError(MSG_CMD_NEEDS_LOADED);
}

//
// PipeAudio
//

PipeAudio::PipeAudio(std::unique_ptr<AudioSource> &&src,
                     std::unique_ptr<AudioSink> &&sink)
    : src(std::move(src)), sink(std::move(sink)), announced_time(false)
{
	this->ClearFrame();
}

std::unique_ptr<Response> PipeAudio::Emit(const std::string &path,
	bool broadcast)
{
	assert(this->src != nullptr);
	assert(this->sink != nullptr);

	std::unique_ptr<Response> ret;

	std::string value;

	if (path == "/control/state") {
		auto state = this->sink->State();
		auto playing = state == Audio::State::PLAYING;
		value = playing ? "Playing" : "Stopped";
	} else if (path == "/player/file") {
		value = this->src->Path();
	} else if (path == "/player/time/elapsed") {
		std::uint64_t micros = this->Position();

		// Always announce a unicast.
		// Only announce broadcasts if CanAnnounceTime(...).
		bool can = (!broadcast) || this->CanAnnounceTime(micros);
		if (!can) return ret;
		value = std::to_string(micros);
	} else return ret;

	ret = std::move(Response::Res("Entry", path, value));
	return ret;
}

void PipeAudio::SetPlaying(bool playing)
{
	assert(this->sink != nullptr);

	if (playing) {
		this->sink->Start();
	} else {
		this->sink->Stop();
	}
}

std::uint64_t PipeAudio::Position() const
{
	assert(this->sink != nullptr);
	assert(this->src != nullptr);

	return this->src->MicrosFromSamples(this->sink->Position());
}

void PipeAudio::Seek(std::uint64_t position)
{
	assert(this->sink != nullptr);
	assert(this->src != nullptr);

	auto in_samples = this->src->SamplesFromMicros(position);
	auto out_samples = this->src->Seek(in_samples);
	this->sink->SetPosition(out_samples);

	// Make sure we always announce the new position to all response sinks.
	this->announced_time = false;

	// We might still have decoded samples from the old position in
	// our frame, so clear them out.
	this->ClearFrame();
}

void PipeAudio::ClearFrame()
{
	this->frame.clear();
	this->frame_iterator = this->frame.end();
}

Audio::State PipeAudio::Update()
{
	assert(this->sink != nullptr);
	assert(this->src != nullptr);

	bool more_available = this->DecodeIfFrameEmpty();
	if (!more_available) this->sink->SourceOut();

	if (!this->FrameFinished()) this->TransferFrame();

	return this->sink->State();
}

void PipeAudio::TransferFrame()
{
	assert(!this->frame.empty());
	assert(this->sink != nullptr);
	assert(this->src != nullptr);

	this->sink->Transfer(this->frame_iterator, this->frame.end());

	// We empty the frame once we're done with it.  This
	// maintains FrameFinished(), as an empty frame is a finished one.
	if (this->FrameFinished()) {
		this->ClearFrame();
		assert(this->FrameFinished());
	}

	// The frame iterator should be somewhere between the beginning and
	// end of the frame, unless the frame was emptied.
	assert(this->frame.empty() ||
	       (this->frame.begin() <= this->frame_iterator &&
	        this->frame_iterator < this->frame.end()));
}

bool PipeAudio::DecodeIfFrameEmpty()
{
	// Either the current frame is in progress, or has been emptied.
	// AdvanceFrameIterator() establishes this assertion by emptying a
	// frame as soon as it finishes.
	assert(this->frame.empty() || !this->FrameFinished());

	// If we still have a frame, don't bother decoding yet.
	if (!this->FrameFinished()) return true;

	assert(this->src != nullptr);
	AudioSource::DecodeResult result = this->src->Decode();

	this->frame = result.second;
	this->frame_iterator = this->frame.begin();

	return result.first != AudioSource::DecodeState::END_OF_FILE;
}

bool PipeAudio::FrameFinished() const
{
	return this->frame.end() <= this->frame_iterator;
}

bool PipeAudio::CanAnnounceTime(std::uint64_t micros)
{
	std::uint64_t secs = micros / 1000 / 1000;

	// We can announce if we don't have a record for this sink, or if the
	// last record was in a previous second.
	bool announce = (!(this->announced_time)) || (this->last_time < secs);

	if (announce) {
		this->announced_time = true;
		this->last_time = secs;
	}

	return announce;
}
