# Upstream ydotool ships ydotoold.service as a systemd *user* unit
# (/usr/lib/systemd/user/) but still declares SYSTEMD_SERVICE, so the generated
# postinst runs `systemctl preset ydotoold.service` against the *system*
# manager -> "Unit file ydotoold.service does not exist" -> postinst fails ->
# package left unconfigured (and anything depending on it blocked).
#
# kiosk-rdp launches ydotoold directly with an explicit socket path, so we
# don't need the service registered at all. Clear SYSTEMD_SERVICE to drop the
# broken preset. The user unit file still ships, harmlessly unused.
SYSTEMD_SERVICE:${PN} = ""
