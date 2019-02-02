//
// Created by _as on 2019-02-01.
//

#pragma once

#include <algorithm>
#include <libvirt/libvirt.h>
#include "../type_ops.hpp"
#include "../Domain.hpp"
#include "../TypesParam.hpp"
#include "Connection.hpp"

namespace virt {
  inline Domain::Domain(virDomainPtr ptr) noexcept : underlying(ptr) {}

  inline Domain::~Domain() noexcept {
    virDomainFree(underlying);
  }

  Domain Domain::createXML(Connection& c, gsl::czstring<> xml, CreateFlags flags) {
    return Domain{virDomainCreateXML(c.underlying, xml, to_integral(flags))};

  }

  void Domain::create(){
    if(virDomainCreate(underlying))
      throw std::runtime_error{"virDomainCreate"};
  }

  Domain::Info Domain::getInfo() const {
    virDomainInfo info;
    virDomainGetInfo(underlying, &info);
    return info;
  }

  unsigned char Domain::getUUID() const {
    unsigned char ret;
    const auto res = virDomainGetUUID(underlying, &ret);
    if(res < 0)
      throw std::runtime_error{"virDomainGetUUID"};
    return ret;
  }

  bool Domain::isActive() const {
    return virDomainIsActive(underlying) != 0;
  }

  void Domain::reboot(Domain::Shutdown::Flags flags){
    virDomainReboot(underlying, to_integral(flags));
  }
  void Domain::rename(gsl::czstring<> name){
    virDomainRename(underlying, name, 0);
  }
  void Domain::reset(){
    virDomainReset(underlying, 0);
  }
  void Domain::resume() {
    virDomainResume(underlying);
  }
  void Domain::setAutoStart(bool as){
    virDomainSetAutostart(underlying, as ? 1 : 0);
  }
  void Domain::shutdown(Domain::Shutdown::Flags flags){
    virDomainShutdownFlags(underlying, to_integral(flags));
  }
  void Domain::suspend(){
    virDomainSuspend(underlying);
  }
  void Domain::destroy(){
    virDomainDestroy(underlying);
  }
  void Domain::undefine(UndefineFlags flags){
    virDomainUndefineFlags(underlying, to_integral(flags));
  }


  Domain::Stats::Record::Record(const virDomainStatsRecord& from) noexcept : dom(from.dom) {
    params.reserve(static_cast<std::size_t>(from.nparams));
    std::transform(from.params, from.params + from.nparams, std::back_inserter(params), [](const virTypedParameter& tp){return TypedParameter{tp};});
  }

  constexpr inline Domain::Stats::Types operator|(Domain::Stats::Types lhs, Domain::Stats::Types rhs) noexcept {
    return Domain::Stats::Types(to_integral(lhs) | to_integral(rhs));
  }
}