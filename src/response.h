// This file is part of playd.
// playd is licensed under the MIT licence: see LICENSE.txt.

/**
 * @file
 * Declaration of classes pertaining to responses to the client.
 * @see response.cpp
 */

#ifndef PLAYD_IO_RESPONSE_H
#define PLAYD_IO_RESPONSE_H

#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "errors.h"

namespace Playd
{

/// Type of client IDs.
enum ClientId : size_t {
	/// Client ID reserved for broadcasts.
	BROADCAST = 0,
};

/// A response.
class Response
{
public:
	/// Type for message tags.
	using Tag = std::string_view;

	/// The tag for unsolicited messages (not from responses).
	static constexpr Tag NOREQUEST = "!";

	/**
	 * Enumeration of all possible response codes.
	 * @note If you're adding new responses here, update
	 *       Response::STRINGS.
	 * @see Response::STRINGS
	 */
	enum class Code : std::uint8_t {
		OHAI,  ///< Server starting up.
		IAMA,  ///< Server sending its role.
		FLOAD, ///< The loaded file just changed.
		EJECT, ///< The loaded file just ejected.
		POS,   ///< Server sending current song time.
		END,   ///< The loaded file just ended.
		PLAY,  ///< The loaded file is playing.
		STOP,  ///< The loaded file has stopped.
		ACK,   ///< Command result.
		LEN    ///< Server sending song length.
	};

	/// The number of codes, which should agree with Response::Code.
	static constexpr std::uint8_t CODE_COUNT = 10;

	/**
	 * Constructs a Response with no arguments.
	 * @param tag The tag of the response.
	 * @param code The Response::Code representing the response command.
	 */
	Response(Tag tag, Response::Code code);

	/**
	 * Adds an argument to this Response.
	 * @param arg The argument to add.  The argument must not be escaped.
	 * @return A reference to this Response, for chaining.
	 */
	Response &AddArg(Tag arg);

	/**
	 * Packs the Response, converting it to a BAPS3 protocol message.
	 * Pack()ing does not alter the Response, which may be Pack()ed again.
	 * @return The BAPS3 message, sans newline, ready to send.
	 */
	[[nodiscard]] std::string Pack() const;

	/**
	 * Shortcut for constructing a final response to a successful request.
	 * @param tag The tag of the original request.
	 * @return A Response acknowledging a successful request.
	 */
	static Response Success(Tag tag);

	/**
	 * Shortcut for constructing a final response to a invalid request.
	 * @param tag The tag of the original request.
	 * @param msg The failure message.
	 * @return A Response acknowledging an invalid request.
	 */
	static Response Invalid(Tag tag, std::string_view msg);

	/**
	 * Shortcut for constructing a final response to a failed request.
	 * @param tag The tag of the original request.
	 * @param msg The failure message.
	 * @return A Response acknowledging a failed request.
	 */
	static Response Failure(Tag tag, std::string_view msg);

private:
	/**
	 * Escapes a single response argument.
	 * @param arg The argument to escape.
	 * @return The escaped argument.
	 */
	static std::string EscapeArg(std::string_view arg);

	/// The current packed form of the response.
	/// @see Pack
	std::string string;
};

/**
 * Abstract class for anything that can be sent a response.
 */
class ResponseSink
{
public:
	/// Empty virtual destructor for Response_sink.
	virtual ~ResponseSink() = default;

	/**
	 * Outputs a response.
	 * @param id The ID of the client of the Response_sink receiving this
	 * response.
	 *   Use 0 for broadcasts.
	 * @param response The Response to output.
	 */
	virtual void Respond(ClientId id, const Response &response) const;
};

} // namespace Playd

#endif // PLAYD_IO_RESPONSE_H
