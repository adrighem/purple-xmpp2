-- Test-only Prosody config for purple-xmpp2. Never use in production.
-- Plaintext auth is allowed because everything runs on loopback.
admins = { }
pidfile = "/tmp/prosody.pid"
log = { { levels = { min = "info" }, to = "console" } }

-- Server profile: XMPP2_DISABLE is a comma list of features to switch off
-- (carbons, mam, upload); set via run.sh XMPP2_SERVER_PROFILE.
local disabled = {}
for item in (Lua.os.getenv("XMPP2_DISABLE") or ""):gmatch("[^,]+") do
	disabled[item] = true
end
local function without(list)
	local out = {}
	for _, m in Lua.ipairs(list) do
		if not disabled[m] and not (m == "muc_mam" and disabled.mam) then
			out[#out + 1] = m
		end
	end
	return out
end

modules_enabled = without({
	"roster", "saslauth", "tls", "dialback", "disco", "carbons", "pep",
	"private", "blocklist", "vcard4", "vcard_legacy", "version", "uptime",
	"time", "ping", "register", "mam", "offline", "smacks", "csi_simple",
	"http", "admin_adhoc", "admin_shell",
})

-- Belt and braces: never load carbons when the profile disables it.
modules_disabled = disabled.carbons and { "carbons" } or { }

c2s_require_encryption = false
allow_unencrypted_plain_auth = true
s2s_require_encryption = false
authentication = "internal_hashed"
allow_registration = false
consider_bosh_secure = true

-- Short-ish stream management window so resumption tests are meaningful.
smacks_hibernation_time = 300

-- XEP-0313: archive everything by default, keep it for the test run.
default_archive_policy = true
archive_expires_after = "1w"
max_archive_query_results = 100

http_ports = { 5280 }
http_interfaces = { "*" }
https_ports = { }
http_external_url = "http://localhost:" .. (Lua.os.getenv("XMPP2_HTTP_PORT") or "15280") .. "/"

-- XMPP2_HTTPS_PORT (run.sh XMPP2_UPLOAD_HTTPS=1): upload URLs are https, served
-- with the test certificate, so the https upload path can be exercised.
local https_port = Lua.os.getenv("XMPP2_HTTPS_PORT")
if https_port then
	https_ports = { 5281 }
	https_interfaces = { "*" }
	https_ssl = {
		certificate = "/etc/prosody/certs/localhost.crt";
		key = "/etc/prosody/certs/localhost.key";
	}
	http_external_url = "https://localhost:" .. https_port .. "/"
end

VirtualHost "localhost"

Component "conference.localhost" "muc"
	modules_enabled = without({ "muc_mam" })
	restrict_room_creation = false
	muc_room_locking = false  -- no instant-room dance for the first joiner
	muc_room_default_persistent = true
	muc_room_default_public = true

if not disabled.upload then
Component "upload.localhost" "http_file_share"
	-- XMPP2_UPLOAD_LIMIT (bytes) raises the limit for the streaming tests
	http_file_share_size_limit = tonumber(Lua.os.getenv("XMPP2_UPLOAD_LIMIT") or "") or 10485760
	http_file_share_expires_after = "1w"
	http_host = "localhost"
end
