-- Test-only Prosody config for purple-xmpp2. Never use in production.
-- Plaintext auth is allowed because everything runs on loopback.
admins = { }
pidfile = "/tmp/prosody.pid"
log = { { levels = { min = "info" }, to = "console" } }

modules_enabled = {
	"roster"; "saslauth"; "tls"; "dialback"; "disco"; "carbons"; "pep";
	"private"; "blocklist"; "vcard4"; "vcard_legacy"; "version"; "uptime";
	"time"; "ping"; "register"; "mam"; "smacks"; "csi_simple"; "http";
	"admin_adhoc";
}

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
max_archive_query_results = 50

http_ports = { 5280 }
http_interfaces = { "*" }
https_ports = { }
http_external_url = "http://localhost:15280/"

VirtualHost "localhost"

Component "conference.localhost" "muc"
	modules_enabled = { "muc_mam" }
	restrict_room_creation = false
	muc_room_default_persistent = true
	muc_room_default_public = true

Component "upload.localhost" "http_file_share"
	http_file_share_size_limit = 10485760
	http_file_share_expires_after = "1w"
	http_host = "localhost"
