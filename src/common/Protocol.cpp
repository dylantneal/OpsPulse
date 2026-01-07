#include "common/Protocol.hpp"
#include <cstring>
#include <algorithm>

namespace opspulse {

void Protocol::writeBE32(uint8_t* dest, uint32_t value) {
    dest[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    dest[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dest[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dest[3] = static_cast<uint8_t>(value & 0xFF);
}

uint32_t Protocol::readBE32(const uint8_t* src) {
    return (static_cast<uint32_t>(src[0]) << 24) |
           (static_cast<uint32_t>(src[1]) << 16) |
           (static_cast<uint32_t>(src[2]) << 8) |
           static_cast<uint32_t>(src[3]);
}

std::vector<uint8_t> Protocol::encode(const std::string& payload) {
    return encode(std::vector<uint8_t>(payload.begin(), payload.end()));
}

std::vector<uint8_t> Protocol::encode(const std::vector<uint8_t>& payload) {
    if (payload.size() > MAX_PAYLOAD_SIZE) {
        throw ProtocolError("Payload too large: " + std::to_string(payload.size()));
    }

    std::vector<uint8_t> frame(HEADER_SIZE + payload.size());
    writeBE32(frame.data(), static_cast<uint32_t>(payload.size()));
    std::memcpy(frame.data() + HEADER_SIZE, payload.data(), payload.size());
    return frame;
}

bool Protocol::FrameParser::parseHeader() {
    if (buffer_.size() < HEADER_SIZE) {
        return false;
    }

    expectedLen_ = readBE32(buffer_.data());

    if (expectedLen_ > MAX_PAYLOAD_SIZE) {
        error_ = true;
        errorMsg_ = "Frame too large: " + std::to_string(expectedLen_);
        return false;
    }

    headerComplete_ = true;
    return true;
}

Protocol::ParseResult Protocol::FrameParser::feed(const uint8_t* data, size_t len) {
    if (error_) {
        return ParseResult::ERROR;
    }

    // Append data to buffer
    buffer_.insert(buffer_.end(), data, data + len);

    // Try to parse header if not complete
    if (!headerComplete_) {
        if (!parseHeader()) {
            return error_ ? ParseResult::ERROR : ParseResult::NEED_MORE_DATA;
        }
    }

    // Check if we have the complete payload
    size_t totalNeeded = HEADER_SIZE + expectedLen_;
    if (buffer_.size() < totalNeeded) {
        return ParseResult::NEED_MORE_DATA;
    }

    return ParseResult::MESSAGE_COMPLETE;
}

std::string Protocol::FrameParser::getMessage() {
    if (!headerComplete_ || buffer_.size() < HEADER_SIZE + expectedLen_) {
        return "";
    }

    std::string msg(buffer_.begin() + HEADER_SIZE, 
                    buffer_.begin() + HEADER_SIZE + expectedLen_);

    // Remove the consumed frame from buffer (keep any extra bytes)
    buffer_.erase(buffer_.begin(), buffer_.begin() + HEADER_SIZE + expectedLen_);

    // Reset for next message
    headerComplete_ = false;
    expectedLen_ = 0;

    return msg;
}

void Protocol::FrameParser::reset() {
    buffer_.clear();
    expectedLen_ = 0;
    headerComplete_ = false;
    error_ = false;
    errorMsg_.clear();
}

} // namespace opspulse

