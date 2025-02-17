/**
 * @file BinaryConversions.cpp
 * @author  Brian Tomko <brian.j.tomko@nasa.gov>
 *
 * @copyright Copyright (c) 2021 United States Government as represented by
 * the National Aeronautics and Space Administration.
 * No copyright is claimed in the United States under Title 17, U.S.Code.
 * All Other Rights Reserved.
 *
 * @section LICENSE
 * Released under the NASA Open Source Agreement (NOSA)
 * See LICENSE.md in the source root directory for more information.
 */

#include "BinaryConversions.h"
#include "Logger.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/hex.hpp>
#if (BOOST_VERSION >= 106600)
#include <boost/beast/core/detail/base64.hpp>
void BinaryConversions::DecodeBase64(const std::string & strBase64, std::vector<uint8_t> & binaryDecodedMessage) {
    //The memory pointed to by `out` points to valid memory of at least `decoded_size(len)` bytes.
    const std::size_t decodedMinimumSize = boost::beast::detail::base64::decoded_size(strBase64.length());
    binaryDecodedMessage.resize(decodedMinimumSize + 5);
    std::pair<std::size_t, std::size_t> sizesB64 = boost::beast::detail::base64::decode(binaryDecodedMessage.data(), strBase64.data(), strBase64.length());
    binaryDecodedMessage.resize(sizesB64.first); //first is The number of octets written to `out
}

void BinaryConversions::EncodeBase64(const std::vector<uint8_t> & binaryMessage, std::string & strBase64) {
    //The memory pointed to by `out` points to valid memory of at least `encoded_size(len)` bytes.
    const std::size_t encodedMinimumSize = boost::beast::detail::base64::encoded_size(binaryMessage.size());
    //return The number of characters written to `out`. This will exclude any null termination.
    //The resulting string will not be null terminated.
    std::vector<uint8_t> dest(encodedMinimumSize + 5);
    const std::size_t b64StringEncodedSizeNoNullTermination = boost::beast::detail::base64::encode(dest.data(), binaryMessage.data(), binaryMessage.size());
    strBase64 = std::string(dest.data(), dest.data() + b64StringEncodedSizeNoNullTermination);
}
#endif //#if (BOOST_VERSION >= 106600)

void BinaryConversions::BytesToHexString(const std::vector<uint8_t> & bytes, std::string & hexString) {
    BytesToHexString(bytes.data(), bytes.size(), hexString);
}
void BinaryConversions::BytesToHexString(const padded_vector_uint8_t& bytes, std::string& hexString) {
    BytesToHexString(bytes.data(), bytes.size(), hexString);
}
void BinaryConversions::BytesToHexString(const void* data, std::size_t size, std::string& hexString) {
    hexString.resize(0);
    hexString.reserve((size * 2) + 2);
    const uint8_t* const ptr = reinterpret_cast<const uint8_t*>(data);
    boost::algorithm::hex(ptr, ptr + size, std::back_inserter(hexString));
}
void BinaryConversions::BytesToHexString(const std::vector<boost::asio::const_buffer>& bytes, std::string& hexString) {
    hexString.resize(0);
    std::size_t totalSize = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        totalSize += bytes[i].size();
    }
    hexString.reserve((totalSize * 2) + 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const boost::asio::const_buffer& cb = bytes[i];
        const uint8_t* const ptr = reinterpret_cast<const uint8_t*>(cb.data());
        boost::algorithm::hex(ptr, ptr + cb.size(), std::back_inserter(hexString));
    }
}
void BinaryConversions::BytesToHexString(const boost::asio::const_buffer& bytes, std::string& hexString) {
    BytesToHexString(bytes.data(), bytes.size(), hexString);
}

bool BinaryConversions::HexStringToBytes(const std::string& hexString, padded_vector_uint8_t& bytes) {
    bytes.resize(0);
    bytes.reserve(hexString.size() / 2);
    try {
        boost::algorithm::unhex(hexString, std::back_inserter(bytes));
    }
    catch (const boost::algorithm::hex_decode_error&) {
        return false;
    }
    catch (const std::exception& e) {
        LOG_ERROR(hdtn::Logger::SubProcess::none) << "unknown decode error: " << e.what();
        return false;
    }
    return true;
}
bool BinaryConversions::HexStringToBytes(const std::string & hexString, std::vector<uint8_t> & bytes) {
    bytes.resize(0);
    bytes.reserve(hexString.size() / 2);
    try {
        boost::algorithm::unhex(hexString, std::back_inserter(bytes));
    }
    catch (const boost::algorithm::hex_decode_error &) {
        return false;
    }
    catch (const std::exception & e) {
        LOG_ERROR(hdtn::Logger::SubProcess::none) << "unknown decode error: " << e.what();
        return false;
    }
    return true;
}
