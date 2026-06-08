# Enable the RDP backend (rdp-backend.so) so the screen-share plugin can clone
# the live kiosk output to an on-demand RDP server. Pulls freerdp (2.x), which
# matches Weston 10's RDP backend API. The 'screenshare' PACKAGECONFIG is
# already on by default in meta-tegra's weston_10.0.2.bb.
#
# Default is no listener: weston-kiosk.ini sets start-on-startup=false, so the
# nested RDP weston only spawns when triggered (see /usr/bin/kiosk-rdp).
PACKAGECONFIG:append = " rdp"
