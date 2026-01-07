#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <stdexcept>

namespace opspulse {

/**
 * Length-prefixed framing protocol for TCP messages.
 * 
 * Frame format:
 * +----------------+------------------+
 * | Length (4B BE) | Payload (N bytes)|
 * +----------------+------------------+
 * 
 * Length field is a 32-bit big-endian unsigned integer representing
 * the size of the payload (not including the length field itself).
 */
class Protocol {
public:
    static constexpr size_t HEADER_SIZE = 4;
    static constexpr size_t MAX_PAYLOAD_SIZE = 1024 * 1024;  // 1MB

    // Encode a message with length prefix
    static std::vector<uint8_t> encode(const std::string& payload);
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& payload);

    // Frame parsing state machine
    enum class ParseResult {
        NEED_MORE_DATA,
        MESSAGE_COMPLETE,
        ERROR
    };

    /**
     * Frame parser that handles partial reads.
     * Accumulates bytes until a complete message is available.
     */
    class FrameParser {
    public:
        FrameParser() = default;

        // Feed data to the parser
        // Returns true if a complete message is available
        ParseResult feed(const uint8_t* data, size_t len);

        // Get the complete message (valid only after feed returns MESSAGE_COMPLETE)
        std::string getMessage();

        // Reset parser state for next message
        void reset();

        // Check if an error occurred
        bool hasError() const { return error_; }
        std::string getError() const { return errorMsg_; }

    private:
        std::vector<uint8_t> buffer_;
        size_t expectedLen_ = 0;
        bool headerComplete_ = false;
        bool error_ = false;
        std::string errorMsg_;

        bool parseHeader();
    };

    // Utility functions for big-endian encoding/decoding
    static void writeBE32(uint8_t* dest, uint32_t value);
    static uint32_t readBE32(const uint8_t* src);
};

// Exception for protocol errors
class ProtocolError : public std::runtime_error {
public:
    explicit ProtocolError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace opspulse

