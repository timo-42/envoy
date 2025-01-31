#include <regex>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/json/json_loader.h"
#include "source/common/router/header_parser.h"

#include "re2/re2.h"

namespace Envoy {
namespace Router {
static const re2::RE2& getMetadataTranslatorPattern() {
  CONSTRUCT_ON_FIRST_USE(re2::RE2,
                         R"EOF(%(UPSTREAM|DYNAMIC)_METADATA\(\s*(\[(?:.|\r?\n)+?\]\s*)\)%)EOF");
}

// Related to issue 20389. Header formatters are parsed and processed by formatters defined in
// source/common/formatter/substitution_formatter.cc. For backwards compatibility UPSTREAM_METADATA
// and UPSTREAM_METADATA format must be changed. Those formatters used to take a JSON format like
// UPSTREAM_METADATA(["a", "b"]) and substitution formatters use UPSTREAM_METADATA(a:b) format.
// This translator translates UPSTREAM_METADATA and DYNAMIC_METADATA from JSON format to colon
// format.
// TODO(cpakulski): Eventually JSON format should be deprecated in favor of colon format.
std::string HeaderParser::translateMetadataFormat(const std::string& header_value) {
  const re2::RE2& re = getMetadataTranslatorPattern();
  ASSERT(re.ok());
  std::string new_header_value = header_value;
  re2::StringPiece json_array, metadata_type;
  while (re.PartialMatch(new_header_value, re, &metadata_type, &json_array)) {
    std::string new_format;
    TRY_ASSERT_MAIN_THREAD {
      Json::ObjectSharedPtr parsed_params = Json::Factory::loadFromString(json_array.as_string());

      // The given json string may be an invalid object or with an empty object array.
      if (parsed_params == nullptr || parsed_params->asObjectArray().empty()) {
        // return original value
        return new_header_value;
      }
      new_format = parsed_params->asObjectArray()[0]->asString();
      for (size_t i = 1; i < parsed_params->asObjectArray().size(); i++) {
        new_format += ":" + parsed_params->asObjectArray()[i]->asString();
      }

      new_format = "%" + metadata_type.as_string() + "_METADATA(" + new_format + ")%";

      ENVOY_LOG_MISC(
          warn,
          "Header formatter: JSON format of {} parameters has been obsoleted. Use colon format: {}",
          metadata_type.as_string() + "_METADATA", new_format.c_str());

      re2::RE2::Replace(&new_header_value, re, new_format);
    }
    END_TRY
    catch (Json::Exception& e) {
      return new_header_value;
    }
  }

  return new_header_value;
}

static const re2::RE2& getPerRequestTranslatorPattern() {
  CONSTRUCT_ON_FIRST_USE(re2::RE2, R"EOF(%PER_REQUEST_STATE\((.+?)\)%)EOF");
}

// Related to issue 20389.
// Header's formatter PER_REQUEST_STATE(key) is equivalent to substitution
// formatter FILTER_STATE(key:PLAIN). translatePerRequestState method
// translates between these 2 formats.
// TODO(cpakulski): eventually PER_REQUEST_STATE formatter should be deprecated in
// favor of FILTER_STATE.
std::string HeaderParser::translatePerRequestState(const std::string& header_value) {
  const re2::RE2& re = getPerRequestTranslatorPattern();
  ASSERT(re.ok());
  std::string new_header_value = header_value;
  re2::StringPiece required_state;
  while (re.PartialMatch(new_header_value, re, &required_state)) {
    std::string new_format = "%FILTER_STATE(" + required_state.as_string() + ":PLAIN)%";

    ENVOY_LOG_MISC(warn, "PER_REQUEST_STATE header formatter has been obsoleted. Use {}",
                   new_format.c_str());
    re2::RE2::Replace(&new_header_value, re, new_format);
  }
  return new_header_value;
}

} // namespace Router
} // namespace Envoy
