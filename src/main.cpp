// This file is part of playd.
// playd is licensed under the MIT licence: see LICENSE.txt.

/**
 * @file
 * Main entry point and implementation of the playd class.
 * @see main.cpp
 */

#include <algorithm>
#include <charconv>
#include <iostream>

#include "io.h"
#include "messages.h"
#include "player.h"
#include "response.h"

#ifdef WITH_MP3
#include "audio/sources/mp3.h"
#endif // WITH_MP3
#ifdef WITH_SNDFILE
#include "audio/sources/sndfile.h"
#endif // WITH_SNDFILE

namespace Playd
{

/// The default IP hostname on which playd will bind.
constexpr std::string_view DEFAULT_HOST{"0.0.0.0"};

/// The default TCP port on which playd will bind.
constexpr std::string_view DEFAULT_PORT{"1350"};

/// Map from file extensions to Audio_source builder functions.
static const std::map<std::string, Player::SourceFn> SOURCES{
#ifdef WITH_MP3
        {"mp3", Audio::MP3Source::MakeUnique},
#endif // WITH_MP3

#ifdef WITH_SNDFILE
        {"flac", Audio::SndfileSource::MakeUnique},
        {"ogg", Audio::SndfileSource::MakeUnique},
        {"wav", Audio::SndfileSource::MakeUnique},
#endif // WITH_SNDFILE
};

/**
 * Creates a vector of strings from a C-style argument vector.
 * @param argc Program argument count.
 * @param argv Program argument vector.
 * @return The argument vector, as a C++ string vector.
 */
std::vector<std::string_view> MakeArgVector(int argc, char *argv[])
{
	std::vector<std::string_view> args(argc);
	for (int i = 0; i < argc; i++) args[i] = std::string_view{argv[i]};
	return args;
}

int GetDeviceIDFromArg(const std::string_view arg)
{
	auto id = -1;

	// MSVC doesn't support casting of string_view_iterator to const char*,
	// so we have to do some strange pointer fun.
	const auto begin_ptr = &*arg.cbegin();
	// Parse, but only accept valid numbers (for which ec is empty).
	auto [p, ec] = std::from_chars(begin_ptr, begin_ptr + arg.size(), id);
	if (ec == std::errc::invalid_argument) {
		std::cerr << "not a valid device ID: " << arg << std::endl;
		return -1;
	}
	if (ec == std::errc::result_out_of_range) {
		std::cerr << "device ID too large: " << arg << std::endl;
		return -1;
	}

	// Only allow valid, outputtable devices; reject input-only devices.
	if (!Audio::SDLSink::IsOutputDevice(id)) return -1;

	return id;
}

/**
 * Tries to get the output device ID from program arguments.
 * @param args The program argument vector.
 * @return The device ID, -1 if invalid selection (or none).
 */
int GetDeviceID(const std::vector<std::string_view> &args)
{
	// Did the user provide an ID at all?
	if (args.size() < 2) return -1;

	return GetDeviceIDFromArg(args.at(1));
}

/**
 * Reports usage information and exits.
 * @param progname The name of the program as executed.
 */
void ExitWithUsage(std::string_view progname)
{
	std::cerr << "usage: " << progname << " ID [HOST] [PORT]\n";
	std::cerr << "where ID is one of the following numbers:\n";

	// Show the user the valid device IDs they can use.
	auto device_list = Audio::SDLSink::GetDevicesInfo();
	for (const auto &device : device_list) {
		std::cerr << "\t" << device.first << ": " << device.second << "\n";
	}

	std::cerr << "default HOST: " << DEFAULT_HOST << "\n";
	std::cerr << "default PORT: " << DEFAULT_PORT << "\n";

	exit(EXIT_FAILURE);
}

/**
 * Gets the host and port from the program arguments.
 * The default arguments are used if the host and/or port are not supplied.
 * @param args The program argument vector.
 * @return A pair of strings representing the hostname and port.
 */
std::pair<std::string_view, std::string_view> GetHostAndPort(const std::vector<std::string_view> &args)
{
	const auto size = args.size();
	return std::make_pair(size > 2 ? args.at(2) : DEFAULT_HOST, size > 3 ? args.at(3) : DEFAULT_PORT);
}

/**
 * Exits with an error message for a network error.
 * @param host The IP host to which playd tried to bind.
 * @param port The TCP port to which playd tried to bind.
 * @param msg The exception's error message.
 */
void ExitWithNetError(std::string_view host, std::string_view port, std::string_view msg)
{
	std::cerr << "Network error: " << msg << "\n";
	std::cerr << "Is " << host << ":" << port << " available?\n";
	exit(EXIT_FAILURE);
}

/**
 * Exits with an error message for an unhandled exception.
 * @param msg The exception's error message.
 */
void ExitWithError(std::string_view msg)
{
	std::cerr << "Unhandled exception in main loop: " << msg << std::endl;
	exit(EXIT_FAILURE);
}
} // namespace Playd

/**
 * The main entry point.
 * @param argc Program argument count.
 * @param argv Program argument vector.
 * @return The exit code (zero for success; non-zero otherwise).
 */
int main(int argc, char *argv[])
{
#ifndef _WIN32
	// If we don't ignore SIGPIPE, certain classes of connection droppage
	// will crash our program with it.
	// TODO(CaptainHayashi): a more rigorous ifndef here.
	signal(SIGPIPE, SIG_IGN);
#endif

	// SDL requires some cleanup and teardown.
	// This call needs to happen before GetDeviceID, otherwise no device
	// IDs will be recognised.  (This is why it's here, and not in
	// SetupAudioSystem.)
	Playd::Audio::SDLSink::InitLibrary();
	atexit(Playd::Audio::SDLSink::CleanupLibrary);

#ifdef WITH_MP3
	// mpg123 insists on us running its init and exit functions, too.
	mpg123_init();
	atexit(mpg123_exit);
#endif // WITH_MP3

	auto args = Playd::MakeArgVector(argc, argv);

	auto device_id = Playd::GetDeviceID(args);
	if (device_id < 0) Playd::ExitWithUsage(args.at(0));

	Playd::Player player{device_id, &std::make_unique<Playd::Audio::SDLSink, const Playd::Audio::Source &, int>,
	                     Playd::SOURCES};

	// Set up the IO now (to avoid a circular dependency).
	// Make sure the player broadcasts its responses back to the IoCore.
	Playd::IO::Core io{player};
	player.SetIo(io);

	// Now, actually run the IO loop.
	auto [host, port] = Playd::GetHostAndPort(args);
	try {
		io.Run(host, port);
	} catch (NetError &e) {
		Playd::ExitWithNetError(host, port, e.Message());
	} catch (Error &e) {
		Playd::ExitWithError(e.Message());
	}

	return EXIT_SUCCESS;
}
