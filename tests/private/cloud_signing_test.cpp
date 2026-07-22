#include <doctest/doctest.h>

#include <cloud_signing.hpp>

#include <vio/crypto.h>

#include <span>
#include <string>

using namespace points::converter::cloud;

static std::span<const uint8_t> bytes(std::string_view s)
{
  return {reinterpret_cast<const uint8_t *>(s.data()), s.size()};
}

TEST_CASE("uri_encode follows RFC 3986")
{
  REQUIRE(uri_encode("blob_00000000_0000000000000000", true) == "blob_00000000_0000000000000000");
  REQUIRE(uri_encode("a/b c", true) == "a/b%20c");   // slash kept, space encoded
  REQUIRE(uri_encode("a/b c", false) == "a%2Fb%20c"); // slash encoded
  REQUIRE(uri_encode("-_.~", true) == "-_.~");        // unreserved never encoded
}

TEST_CASE("vio::crypto sanity (empty SHA256, known HMAC hex)")
{
  // SHA256("") — the payload hash SigV4 uses for empty bodies.
  REQUIRE(vio::crypto::to_hex(vio::crypto::sha256(bytes(""))) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("AWS SigV4 matches the official get-vanilla test vector")
{
  // From the AWS SigV4 test suite (aws-sig-v4-test-suite/get-vanilla): a plain GET with only host and
  // x-amz-date signed, empty body, credentials AKIDEXAMPLE / wJalr..., region us-east-1, service "service".
  std::string empty_sha = vio::crypto::to_hex(vio::crypto::sha256(bytes("")));
  auto authz = aws_sigv4_authorization("GET", "/", "",
                                       {{"host", "example.amazonaws.com"}, {"x-amz-date", "20150830T123600Z"}},
                                       empty_sha,
                                       "AKIDEXAMPLE", "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY",
                                       "us-east-1", "service", "20150830T123600Z", "20150830");
  REQUIRE(authz == "AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/20150830/us-east-1/service/aws4_request, "
                   "SignedHeaders=host;x-amz-date, Signature=5fa00fa31553b73ebf1942676e86291e8372ff2a2260956d9b8aae1d763fbf31");
}

TEST_CASE("AWS SigV4 signs a realistic S3 PUT deterministically")
{
  // Determinism + shape check (fixed date/key -> stable signature). x-amz-content-sha256 is signed.
  std::string body = "hello world";
  std::string body_sha = vio::crypto::to_hex(vio::crypto::sha256(bytes(body)));
  auto authz = aws_sigv4_authorization("PUT", "/bucket/blob_00000001", "",
                                       {{"host", "s3.amazonaws.com"}, {"x-amz-content-sha256", body_sha}, {"x-amz-date", "20240101T000000Z"}},
                                       body_sha, "AKID", "SECRET", "us-east-1", "s3", "20240101T000000Z", "20240101");
  REQUIRE(authz.rfind("AWS4-HMAC-SHA256 Credential=AKID/20240101/us-east-1/s3/aws4_request", 0) == 0);
  REQUIRE(authz.find("SignedHeaders=host;x-amz-content-sha256;x-amz-date") != std::string::npos);
  // 64 hex chars of signature.
  auto pos = authz.find("Signature=");
  REQUIRE(pos != std::string::npos);
  REQUIRE(authz.substr(pos + 10).size() == 64);
}

TEST_CASE("Azure Shared Key produces a stable SharedKey header")
{
  // Account key is base64; signature is base64(HMAC-SHA256(key, string-to-sign)). Deterministic.
  std::string account = "devstoreaccount1";
  // The well-known Azurite/Azure Storage Emulator account key (base64).
  std::string key = "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==";
  auto authz = azure_sharedkey_authorization("GET", account, key,
                                             "/" + account + "/container/blob_00000000_0000000000000000",
                                             {{"x-ms-date", "Mon, 01 Jan 2024 00:00:00 GMT"}, {"x-ms-version", "2021-08-06"}},
                                             "", "", "");
  REQUIRE(authz.rfind("SharedKey devstoreaccount1:", 0) == 0);
  // The base64 signature after the ':' must be non-empty and decode-able.
  auto sig = authz.substr(std::string("SharedKey devstoreaccount1:").size());
  REQUIRE(!sig.empty());
  REQUIRE(vio::crypto::base64_decode(sig).has_value());
}
