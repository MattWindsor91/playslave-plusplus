// This file is part of playd.
// playd is licensed under the MIT licence: see LICENSE.txt.

/**
 * @file
 * Declaration of the Player class, and associated types.
 * @see player.cpp
 */

#ifndef PLAYD_PLAYER_H
#define PLAYD_PLAYER_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio/audio.h"
#include "audio/sink.h"
#include "audio/source.h"
#include "response.h"

namespace Playd
{
/**
 * A Player contains a loaded audio file and a command API for manipulating it.
 * @see Audio
 * @see AudioSystem
 */
class Player
{
public:
	/// Type for functions that construct sinks.
	using SinkFn = std::function<std::unique_ptr<Audio::Sink>(const Audio::Source &, int)>;

	/// Type for functions that construct sources.
	using SourceFn = std::function<std::unique_ptr<Audio::Source>(std::string_view)>;

	/**
	 * Constructs a Player.
	 * @param device_id The device ID to which sinks shall output.
	 * @param sink The function to be used for building sinks.
	 * @param sources The map of file extensions to functions used for
	 * building sources.
	 */
	Player(int device_id, SinkFn sink, std::map<std::string, SourceFn> sources);

	/// Deleted copy constructor.
	Player(const Player &) = delete;

	/// Deleted copy-assignment constructor.
	Player &operator=(const Player &) = delete;

	/**
	 * Sets the response sink to which this Player shall send responses.
	 * This sink shall be the target for WelcomeClient, as well as
	 * any responses generated by RunCommand or Update.
	 * @param io The response sink (invariably the IO system).
	 */
	void SetIo(const ResponseSink &io);

	/**
	 * Instructs the Player to perform a cycle of work.
	 * This includes decoding the next frame and responding to commands.
	 * @return Whether the player has more cycles of work to do.
	 */
	bool Update();

	//
	// Commands
	//

	/**
	 * Tells the audio file to start or stop playing.
	 * @param tag The tag of the request calling this command.
	 *   For unsolicited state changes, use Response::NOREQUEST.
	 * @param playing True if playing; false otherwise.
	 * @see Play
	 * @see Stop
	 */
	Response SetPlaying(Response::Tag tag, bool playing);

	/**
	 * Dumps the current player state to the given ID.
	 *
	 * @param id The ID of the connection to which the Player should
	 *   route any responses.  For broadcasts, use 0.
	 * @param tag The tag of the request calling this command.
	 *   For unsolicited dumps, use Response::NOREQUEST.
	 * @return The result of dumping, which is always success.
	 */
	[[nodiscard]] Response Dump(ClientId id, Response::Tag tag) const;

	/**
	 * Ejects the current loaded song, if any.
	 * @param tag The tag of the request calling this command.
	 *   For unsolicited ejects, use Response::NOREQUEST.
	 * @return Whether the ejection succeeded.
	 */
	Response Eject(Response::Tag tag);

	/**
	 * Ends a file, stopping and rewinding.
	 * @param tag The tag of the request calling this command.
	 *   For unsolicited ends, use Response::NOREQUEST.
	 * @return Whether the end succeeded.
	 */
	Response End(Response::Tag tag);

	/**
	 * Loads a file.
	 * @param tag The tag of the request calling this command.
	 *   For unsolicited loads, use Response::NOREQUEST.
	 * @param path The absolute path to a track to load.
	 * @return Whether the load succeeded.
	 */
	Response Load(Response::Tag tag, std::string_view path);

	/**
	 * Seeks to a given position in the current file.
	 * @param tag The tag of the request calling this command.
	 *   For unsolicited seeks, use Response::NOREQUEST.
	 * @param pos_str A string containing a timestamp, in microseconds
	 * @return Whether the seek succeeded.
	 */
	Response Pos(Response::Tag tag, std::string_view pos_str);

	/**
	 * Quits playd.
	 * @param tag The tag of the request calling this command.
	 *   For unsolicited quits, use Response::NOREQUEST.
	 * @return Whether the quit succeeded.
	 */
	Response Quit(Response::Tag tag);

private:
	int device_id;                           ///< The sink's device ID.
	SinkFn sink;                             ///< The sink create function.
	std::map<std::string, SourceFn> sources; ///< The file formats map.
	std::unique_ptr<Audio::Audio> file;      ///< The loaded audio file.
	bool dead;                               ///< Whether the Player is closing.
	const ResponseSink *io;                  ///< The sink for responses.
	std::chrono::seconds last_pos;           ///< The last-sent position.

	/**
	 * Parses pos_str as a seek timestamp.
	 * @param pos_str The time string to be parsed.
	 * @return The parsed time.
	 * @exception std::out_of_range
	 *   See http://www.cplusplus.com/reference/string/stoull/#exceptions
	 * @exception std::invalid_argument
	 *   See http://www.cplusplus.com/reference/string/stoull/#exceptions
	 * @exception SeekError
	 *   Raised if checks beyond those done by stoull fail.
	 */
	static std::chrono::microseconds PosParse(std::string_view pos_str);

	/**
	 * Performs an actual seek.
	 * This does not do any EOF handling.
	 * @param tag The tag of the request calling this command.
	 *   For unsolicited dumps, use Response::NOREQUEST.
	 * @param pos The new position, in microseconds.
	 * @exception Seek_error
	 *   Raised if the seek is out of range (usually EOF).
	 * @see Player::Seek
	 */
	void PosRaw(Response::Tag tag, std::chrono::microseconds pos);

	/**
	 * Emits a response for the current audio state to the sink.
	 *
	 * @param id The ID of the connection to which the Player should
	 *   route any responses.  For broadcasts, use 0.
	 * @param tag The tag of the request calling this command.
	 *   For unsolicited dumps, use Response::NOREQUEST.
	 *
	 * @see DumpFileInfo
	 */
	void DumpState(ClientId id, Response::Tag tag) const;

	/**
	 * Emits responses for the current audio file's metrics to the sink.
	 *
	 * @param id The ID of the connection to which the Player should
	 *   route any responses.  For broadcasts, use 0.
	 * @param tag The tag of the request calling this command.
	 *   For unsolicited dumps, use Response::NOREQUEST.
	 *
	 * @see DumpState
	 */
	void DumpFileInfo(ClientId id, Response::Tag tag) const;

	/**
	 * @return The player's current state as a response code.
	 */
	[[nodiscard]] Response::Code StateResponseCode() const;

	/**
	 * Outputs a response, if there is a ResponseSink attached.
	 *
	 * Otherwise, this method does nothing.
	 * @param id The ID of the client receiving this response.
	 *   Use 0 for broadcasts.
	 * @param response The Response to output.
	 */
	void Respond(ClientId id, const Response &rs) const;

	/**
	 * Sends a timestamp response.
	 *
	 * @param code The command code for the response.
	 * @param id The client ID to send towards.
	 * @param tag The tag to send with the response.
	 * @param ts The value of the response, in microseconds.
	 */
	void AnnounceTimestamp(Response::Code code, ClientId id, Response::Tag tag, std::chrono::microseconds ts) const;

	/**
	 * Determines whether we can broadcast a POS response.
	 *
	 * To prevent spewing massive amounts of POS responses, we only send a
	 * broadcast if the current position, truncated to the last second,
	 * is higher than the last one stored with a broadcast AnnouncePos().
	 *
	 * @param pos The value of the POS response, in microseconds.
	 * @return Whether it is polite to broadcast POS.
	 */
	[[nodiscard]] bool CanBroadcastPos(std::chrono::microseconds pos) const;

	/**
	 * Broadcasts a POS response.
	 *
	 * This updates the last broadcast time, so that CanBroadcastPos()
	 * prevents further broadcasts from happening too soon after this one.
	 *
	 * @param tag The tag of the request that changed the position, if any.
	 *   For regularly scheduled position updates, use Response::NOREQUEST.
	 * @param pos The new position, in microseconds.
	 * @param pos The value of the POS response, in microseconds.
	 */
	void BroadcastPos(Response::Tag tag, std::chrono::microseconds pos);

	//
	// Audio subsystem
	//

	/**
	 * Loads a file, creating an Audio for it.
	 * @param path The path to a file.
	 * @return A unique pointer to the Audio for that file.
	 */
	[[nodiscard]] std::unique_ptr<Audio::Audio> LoadRaw(std::string_view path) const;

	/**
	 * Loads a file, creating an AudioSource.
	 * @param path The path to the file to load.
	 * @return An Audio_source pointer (may be nullptr, if no available
	 *   and suitable Audio_source was found).
	 * @see Load
	 */
	[[nodiscard]] std::unique_ptr<Audio::Source> LoadSource(std::string_view path) const;
};

} // namespace Playd

#endif // PLAYD_PLAYER_H
