#include "extensions/filters/http/cache/cache_filter.h"

#include "envoy/registry/registry.h"

#include "common/config/utility.h"
#include "common/http/headers.h"

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

bool CacheFilter::isCacheableRequest(Http::HeaderMap& headers) {
  const Http::HeaderEntry* method = headers.Method();
  const Http::HeaderEntry* forwarded_proto = headers.ForwardedProto();
  const Http::HeaderValues& header_values = Http::Headers::get();
  // TODO(toddmgreer): Also serve HEAD requests from cache.
  // TODO(toddmgreer): Check all the other cache-related headers.
  return method && forwarded_proto && headers.Path() && headers.Host() &&
         (method->value() == header_values.MethodValues.Get) &&
         (forwarded_proto->value() == header_values.SchemeValues.Http ||
          forwarded_proto->value() == header_values.SchemeValues.Https);
}

bool CacheFilter::isCacheableResponse(Http::HeaderMap& headers) {
  const Http::HeaderEntry* cache_control = headers.CacheControl();
  // TODO(toddmgreer): fully check for cacheability. See for example
  // https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/http/caching_headers.h.
  if (cache_control) {
    return !StringUtil::caseFindToken(cache_control->value().getStringView(), ",",
                                      Http::Headers::get().CacheControlValues.Private);
  }
  return false;
}

HttpCache&
CacheFilter::getCache(const envoy::config::filter::http::cache::v2::CacheConfig& config) {
  return Config::Utility::getAndCheckFactory<HttpCacheFactory>(config).getCache(config);
}

CacheFilter::CacheFilter(const envoy::config::filter::http::cache::v2::CacheConfig& config,
                         const std::string&, Stats::Scope&, TimeSource& time_source)
    : time_source_(time_source), cache_(getCache(config)) {}

void CacheFilter::onDestroy() {
  lookup_ = nullptr;
  insert_ = nullptr;
}

Http::FilterHeadersStatus CacheFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders: {}", *decoder_callbacks_, headers);
  if (!isCacheableRequest(headers)) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders ignoring uncacheable request: {}",
                     *decoder_callbacks_, headers);
    return Http::FilterHeadersStatus::Continue;
  }
  ASSERT(decoder_callbacks_);
  lookup_ = cache_.makeLookupContext(LookupRequest(headers, time_source_.systemTime()));
  ASSERT(lookup_);

  CacheFilterSharedPtr self = shared_from_this();
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders starting lookup", *decoder_callbacks_);
  lookup_->getHeaders([self](LookupResult&& result) { onHeadersAsync(self, std::move(result)); });
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus CacheFilter::encodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  if (lookup_ && isCacheableResponse(headers)) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeHeaders inserting headers", *encoder_callbacks_);
    insert_ = cache_.makeInsertContext(std::move(lookup_));
    insert_->insertHeaders(headers, end_stream);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (insert_) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeHeaders inserting body", *encoder_callbacks_);
    // TODO(toddmgreer): Wait for the cache if necessary.
    insert_->insertBody(
        data, [](bool) {}, end_stream);
  }
  return Http::FilterDataStatus::Continue;
}

void CacheFilter::onOkHeaders(Http::HeaderMapPtr&& headers,
                              std::vector<AdjustedByteRange>&& /*response_ranges*/,
                              uint64_t content_length, bool has_trailers) {
  if (!lookup_) {
    return;
  }
  response_has_trailers_ = has_trailers;
  const bool end_stream = (content_length == 0 && !response_has_trailers_);
  // TODO(toddmgreer): Calculate age per https://httpwg.org/specs/rfc7234.html#age.calculations
  headers->addReferenceKey(Http::Headers::get().Age, 0);
  decoder_callbacks_->encodeHeaders(std::move(headers), end_stream);
  if (end_stream) {
    return;
  }
  if (content_length > 0) {
    remaining_body_.emplace_back(0, content_length);
    getBody();
  } else {
    lookup_->getTrailers([self = shared_from_this()](Http::HeaderMapPtr&& trailers) {
      onTrailersAsync(self, std::move(trailers));
    });
  }
}

void CacheFilter::onUnusableHeaders() {
  if (lookup_) {
    decoder_callbacks_->continueDecoding();
  }
}

void CacheFilter::onHeadersAsync(const CacheFilterSharedPtr& self, LookupResult&& result) {
  switch (result.cache_entry_status_) {
  case CacheEntryStatus::RequiresValidation:
  case CacheEntryStatus::FoundNotModified:
  case CacheEntryStatus::UnsatisfiableRange:
    ASSERT(false); // We don't yet return or support these codes.
    FALLTHRU;
  case CacheEntryStatus::Unusable: {
    self->post([self] { self->onUnusableHeaders(); });
    return;
  }
  case CacheEntryStatus::Ok:
    self->post([self, headers = result.headers_.release(),
                response_ranges = std::move(result.response_ranges_),
                content_length = result.content_length_,
                has_trailers = result.has_trailers_]() mutable {
      self->onOkHeaders(absl::WrapUnique(headers), std::move(response_ranges), content_length,
                        has_trailers);
    });
  }
}

void CacheFilter::getBody() {
  ASSERT(!remaining_body_.empty(), "No reason to call getBody when there's no body to get.");
  CacheFilterSharedPtr self = shared_from_this();
  lookup_->getBody(remaining_body_[0],
                   [self](Buffer::InstancePtr&& body) { self->onBody(std::move(body)); });
}

void CacheFilter::onBodyAsync(const CacheFilterSharedPtr& self, Buffer::InstancePtr&& body) {
  self->post([self, body = body.release()] { self->onBody(absl::WrapUnique(body)); });
}

// TODO(toddmgreer): Handle downstream backpressure.
void CacheFilter::onBody(Buffer::InstancePtr&& body) {
  if (!lookup_) {
    return;
  }
  if (remaining_body_.empty()) {
    ASSERT(false, "CacheFilter doesn't call getBody unless there's more body to get, so this is a "
                  "bogus callback.");
    decoder_callbacks_->resetStream();
    return;
  }

  if (!body) {
    ASSERT(false, "Cache said it had a body, but isn't giving it to us.");
    decoder_callbacks_->resetStream();
    return;
  }

  const uint64_t bytes_from_cache = body->length();
  if (bytes_from_cache < remaining_body_[0].length()) {
    remaining_body_[0].trimFront(bytes_from_cache);
  } else if (bytes_from_cache == remaining_body_[0].length()) {
    remaining_body_.erase(remaining_body_.begin());
  } else {
    ASSERT(false, "Received oversized body from cache.");
    decoder_callbacks_->resetStream();
    return;
  }

  const bool end_stream = remaining_body_.empty() && !response_has_trailers_;
  decoder_callbacks_->encodeData(*body, end_stream);
  if (!remaining_body_.empty()) {
    getBody();
  } else if (response_has_trailers_) {
    lookup_->getTrailers([self = shared_from_this()](Http::HeaderMapPtr&& trailers) {
      onTrailersAsync(self, std::move(trailers));
    });
  }
}

void CacheFilter::onTrailers(Http::HeaderMapPtr&& trailers) {
  if (lookup_) {
    decoder_callbacks_->encodeTrailers(std::move(trailers));
  }
}

void CacheFilter::onTrailersAsync(const CacheFilterSharedPtr& self, Http::HeaderMapPtr&& trailers) {
  self->post(
      [self, trailers = trailers.release()] { self->onTrailers(absl::WrapUnique(trailers)); });
}

void CacheFilter::post(std::function<void()> f) const {
  decoder_callbacks_->dispatcher().post(std::move(f));
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
