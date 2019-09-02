#pragma once
#include <array>
#include <functional>
#include <string_view>
#include <gsl/gsl>
#include "cexpr_algs.hpp"
#include "depends.hpp"
#include "json2virt.hpp"
#include "json_utils.hpp"
#include "utils.hpp"

#define PM_LIFT(mem_fn) [&](auto... args) { return mem_fn(args...); }
#define PM_PREREQ(...) [&] { __VA_ARGS__ return DependsOutcome::SUCCESS; }

using namespace std::literals;

constexpr auto action_scope = [](auto&&... actions) {
    using Arr = std::array<std::function<DependsOutcome()>, sizeof...(actions)>; // pray for SFO ; wait for expansion statements
    for (auto&& action : Arr{actions...}) {
        if (const auto ao = action(); ao != DependsOutcome::SKIPPED)
            return ao;
    }
    return DependsOutcome::SKIPPED;
};

template <class CRTP, class Hdl> class NamedCallTable {
    constexpr auto& keys() const noexcept { return static_cast<const CRTP&>(*this).keys; }
    constexpr auto& fcns() const noexcept { return static_cast<const CRTP&>(*this).fcns; }

  public:
    constexpr Hdl operator[](std::string_view sv) const noexcept {
        const auto it = cexpr::find(keys().begin(), keys().end(), sv);
        if (it == keys().end())
            return nullptr;
        const auto idx = std::distance(keys().begin(), it);
        return fcns()[idx];
    }
};

using DomainActionsHdl = DependsOutcome (*)(const rapidjson::Value& val, JsonRes& json_res, virt::Domain& dom, std::string_view key_str);
class DomainActionsTable : public NamedCallTable<DomainActionsTable, DomainActionsHdl> {
  private:
    friend NamedCallTable<DomainActionsTable, DomainActionsHdl>;
    template <typename Flag, typename Flags>
    constexpr static const auto getFlag = [](const rapidjson::Value& json_flag, auto error) {
        if (auto v = Flags{}[std::string_view{json_flag.GetString(), json_flag.GetStringLength()}]; v)
            return std::optional{*v};
        return error(301), std::optional<Flag>{std::nullopt};
    };
    template <class F, class Fs> static std::optional<F> getCombinedFlags(const rapidjson::Value& json_flag, JsonRes& json_res) noexcept {
        auto error = [&](auto... args) { return json_res.error(args...), std::nullopt; };
        auto getFlag = DomainActionsTable::getFlag<F, Fs>;

        F flagset{};
        if (json_flag.IsArray()) {
            const auto json_arr = json_flag.GetArray();
            if constexpr (test_sfinae([](auto f) -> decltype(f | f) {}, F{})) {
                for (const auto& json_str : json_arr) {
                    const auto v = getFlag(json_str, error);
                    if (!v)
                        return std::nullopt;
                    flagset |= *v;
                }
            } else {
                if (json_arr.Size() > 1)
                    return error(301);
                return {json_arr.Empty() ? F{} : getFlag(json_arr[0], error)};
            }
        } else if (json_flag.IsString()) {
            const auto v = getFlag(json_flag, error);
            if (!v)
                return std::nullopt;
            flagset = *v;
        }
        return {flagset};
    }

    using Hdl = DomainActionsHdl;

    constexpr static std::array<std::string_view, 7> keys = {"power_mgt", "name", "memory", "max_memory", "autostart", "send_signal", "send_keys"};
    constexpr static std::array<Hdl, 7> fcns = {
        +[](const rapidjson::Value& val, JsonRes& json_res, virt::Domain& dom, std::string_view key_str) -> DependsOutcome {
            auto error = [&](auto... args) { return json_res.error(args...), DependsOutcome::FAILURE; };
            auto pm_message = [&](gsl::czstring<> name, gsl::czstring<> value) {
                rapidjson::Value msg_val{};
                msg_val.SetObject();
                msg_val.AddMember(rapidjson::StringRef(name), rapidjson::StringRef(value), json_res.GetAllocator());
                json_res.message(msg_val);
                return DependsOutcome::SUCCESS;
            };

            constexpr auto getShutdownFlag = getFlag<virt::Domain::ShutdownFlag, virt::Domain::ShutdownFlagsC>;

            const rapidjson::Value* json_flag = nullptr;
            gsl::czstring<> pm_req{};

            if (val.IsString()) {
                pm_req = val.GetString();
            } else if (val.IsObject()) {
                auto it_req = val.FindMember("request");
                auto it_flags = val.FindMember("type");
                if (it_req == val.MemberEnd())
                    return error(300);
                if (it_flags != val.MemberEnd())
                    json_flag = &it_flags->value;
                pm_req = it_req->value.GetString();
            } else {
                return error(300);
            }

            const auto dom_state = virt::Domain::State{dom.getInfo().state};

            const auto pm_hdl = [&](gsl::czstring<> req_tag, auto flags, auto mem_fcn, int errc, gsl::czstring<> pm_msg, auto prereqs) {
                using Flag = typename decltype(flags)::First;
                using FlagsC = typename decltype(flags)::Second;
                return [=, &json_flag, &json_res]() {
                    const auto local_error = [&] {
                        const auto err_msg = error_messages[errc];
                        logger.error(err_msg, " :", key_str);
                        return error(errc);
                    };

                    if (pm_req == std::string_view{req_tag}) {
                        if (prereqs() == DependsOutcome::FAILURE)
                            return DependsOutcome::FAILURE;
                        if (json_flag) {
                            if constexpr (test_sfinae([](auto f) -> std::enable_if_t<!std::is_same_v<decltype(f), Empty>> {}, Flag{})) {
                                constexpr const auto getFlags = getCombinedFlags<Flag, FlagsC>;
                                const auto o_flagset = getFlags(*json_flag, json_res);
                                if (!o_flagset)
                                    return DependsOutcome::FAILURE;
                                if (const auto flagset = *o_flagset; !mem_fcn(flagset))
                                    return local_error();
                            } else
                                return error(301);
                        } else {
                            if constexpr (test_sfinae([](auto f) { f(); }, mem_fcn)) {
                                if (!mem_fcn())
                                    local_error();
                            } else
                                return error(301);
                        }
                        return pm_message(req_tag, pm_msg);
                    }
                    return DependsOutcome::SKIPPED;
                };
            };
            constexpr auto no_flags = tp<Empty, Empty>;
            return action_scope(
                pm_hdl("shutdown", tp<virt::Domain::ShutdownFlag, virt::Domain::ShutdownFlagsC>, PM_LIFT(dom.shutdown), 200,
                       "Domain is being shutdown", PM_PREREQ(if (dom_state != virt::Domain::State::RUNNING) return error(201);)),
                pm_hdl("destroy", tp<virt::Domain::DestroyFlag, virt::Domain::DestroyFlagsC>, PM_LIFT(dom.destroy), 209, "Domain destroyed",
                       PM_PREREQ(if (!dom.isActive()) return error(210);)),
                pm_hdl("start", tp<virt::Domain::CreateFlag, virt::Domain::CreateFlagsC>, PM_LIFT(dom.create), 202, "Domain started",
                       PM_PREREQ(if (dom.isActive()) return error(203);)),
                pm_hdl("reboot", tp<virt::Domain::ShutdownFlag, virt::Domain::ShutdownFlagsC>, PM_LIFT(dom.reboot), 213, "Domain is being rebooted",
                       PM_PREREQ(if (dom_state != virt::Domain::State::RUNNING) return error(201);)),
                pm_hdl("reset", no_flags, PM_LIFT(dom.reset), 214, "Domain was reset", PM_PREREQ(if (!dom.isActive()) return error(210);)),
                pm_hdl("suspend", no_flags, PM_LIFT(dom.suspend), 215, "Domain suspended",
                       PM_PREREQ(if (dom_state != virt::Domain::State::RUNNING) return error(201);)),
                pm_hdl("resume", no_flags, PM_LIFT(dom.resume), 212, "Domain resumed",
                       PM_PREREQ(if (dom_state != virt::Domain::State::PAUSED) return error(211);)),
                [&]() { return error(300); });
        },
        +[](const rapidjson::Value& val, JsonRes& json_res, virt::Domain& dom, std::string_view) -> DependsOutcome {
            auto error = [&](auto... args) { return json_res.error(args...), DependsOutcome::FAILURE; };
            if (!val.IsString())
                return error(0);
            if (!dom.rename(val.GetString()))
                return error(205);
            return DependsOutcome::SUCCESS;
        },
        +[](const rapidjson::Value& val, JsonRes& json_res, virt::Domain& dom, std::string_view) -> DependsOutcome {
            auto error = [&](auto... args) { return json_res.error(args...), DependsOutcome::FAILURE; };
            if (!val.IsInt())
                return error(0);
            if (!dom.setMemory(val.GetInt()))
                return error(206);
            return DependsOutcome::SUCCESS;
        },
        +[](const rapidjson::Value& val, JsonRes& json_res, virt::Domain& dom, std::string_view) -> DependsOutcome {
            auto error = [&](auto... args) { return json_res.error(args...), DependsOutcome::FAILURE; };
            if (!val.IsInt())
                return error(0);
            if (!dom.setMaxMemory(val.GetInt()))
                return error(207);
            return DependsOutcome::SUCCESS;
        },
        +[](const rapidjson::Value& val, JsonRes& json_res, virt::Domain& dom, std::string_view) -> DependsOutcome {
            auto error = [&](auto... args) { return json_res.error(args...), DependsOutcome::FAILURE; };
            if (!val.IsBool())
                return error(0);
            if (!dom.setAutoStart(val.GetBool()))
                return error(208);
            return DependsOutcome::SUCCESS;
        },
        +[](const rapidjson::Value& val, JsonRes& json_res, virt::Domain& dom, std::string_view key_str) -> DependsOutcome {
            auto error = [&](auto... args) { return json_res.error(args...), DependsOutcome::FAILURE; };
            if (!val.IsObject())
                return error(0);

            const auto pid_opt = extract_param<JTag::Int64>(val, "pid", json_res);
            if (!pid_opt)
                return error(0);
            const long long pid = *pid_opt;

            const auto sig_opt =
                extract_param<JTag::Enum, JTag::None, TypePair<virt::Domain::ProcessSignal, virt::Domain::ProcessSignalsC>>(val, "signal", json_res);
            if (!sig_opt)
                return error(0);
            const auto sig = *sig_opt;

            return dom.sendProcessSignal(pid, sig) ? DependsOutcome::SUCCESS : DependsOutcome::FAILURE;
        },
        +[](const rapidjson::Value& val, JsonRes& json_res, virt::Domain& dom, std::string_view key_str) -> DependsOutcome {
            auto error = [&](auto... args) { return json_res.error(args...), DependsOutcome::FAILURE; };
            if (!val.IsObject())
                return error(0);

            const auto keycodeset_opt =
                extract_param<JTag::Enum, JTag::None, TypePair<virt::Domain::KeycodeSet, virt::Domain::KeycodeSetsC>>(val, "keycode_set", json_res);
            if (!keycodeset_opt)
                return error(0);
            const auto keycodeset = *keycodeset_opt;

            const auto holdtime_opt = extract_param<JTag::Uint32>(val, "hold_time", json_res);
            if (!holdtime_opt)
                return error(0);
            const auto holdtime = *holdtime_opt;

            const auto keys_opt = extract_param<JTag::Array, JTag::Uint32>(val, "keys", json_res);
            if (!keys_opt)
                return error(0);
            const auto keys = *keys_opt;

            return dom.sendKey(keycodeset, holdtime, gsl::span(keys.data(), keys.size())) ? DependsOutcome::SUCCESS : DependsOutcome::FAILURE;
        }};
    static_assert(keys.size() == fcns.size());
} constexpr static const domain_actions_table{};