#pragma once
#include <string>
#include <vector>

#define CONTAINS(r,v) (std::find(std::begin(r), std::end(r), v) != std::end(r))

const char* const kFeatureShell2 = "shell_v2";
const char* const kFeatureCmd = "cmd";
const char* const kFeatureStat2 = "stat_v2";
const char* const kFeatureLs2 = "ls_v2";
const char* const kFeatureLibusb = "libusb";
const char* const kFeaturePushSync = "push_sync";
const char* const kFeatureApex = "apex";
const char* const kFeatureFixedPushMkdir = "fixed_push_mkdir";
const char* const kFeatureAbb = "abb";
const char* const kFeatureFixedPushSymlinkTimestamp = "fixed_push_symlink_timestamp";
const char* const kFeatureAbbExec = "abb_exec";
const char* const kFeatureRemountShell = "remount_shell";
const char* const kFeatureTrackApp = "track_app";
const char* const kFeatureSendRecv2 = "sendrecv_v2";
const char* const kFeatureSendRecv2Brotli = "sendrecv_v2_brotli";
const char* const kFeatureSendRecv2LZ4 = "sendrecv_v2_lz4";
const char* const kFeatureSendRecv2Zstd = "sendrecv_v2_zstd";
const char* const kFeatureSendRecv2DryRunSend = "sendrecv_v2_dry_run_send";
// TODO(joshuaduong): Bump to v2 when openscreen discovery is enabled by default
const char* const kFeatureOpenscreenMdns = "openscreen_mdns";

constexpr char kShellServiceArgRaw[] = "raw";
constexpr char kShellServiceArgPty[] = "pty";
constexpr char kShellServiceArgShellProtocol[] = "v2";


std::vector<std::string> adb_get_feature(std::string m_serial, uint64_t* m_transport_id, std::string* error);
bool CanUseFeature(const std::vector<std::string>& feature_set, const std::string& feature);
std::string ShellServiceString(bool use_shell_protocol, const std::string& type_arg, const std::string& command);
std::string format_host_command(std::string m_serial, uint64_t* m_transport_id, const char* command);