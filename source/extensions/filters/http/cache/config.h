#pragma once

#include "envoy/config/filter/http/cache/v2/cache.pb.h"
#include "envoy/config/filter/http/cache/v2/cache.pb.validate.h"

#include "extensions/filters/http/cache/cache_filter.h"
#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilterFactory
    : public Common::FactoryBase<envoy::config::filter::http::cache::v2::CacheConfig> {
public:
  CacheFilterFactory() : FactoryBase(HttpFilterNames::get().Cache) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::cache::v2::CacheConfig& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
