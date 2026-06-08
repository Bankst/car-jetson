# freerdp is pulled in only as the backing lib for Weston's RDP *server*
# backend (rdp-backend.so). Weston needs the server library and nothing else,
# so strip the client-side channel features the default recipe enables —
# cups (printer redirection), pcsc (smartcard), gstreamer, pulseaudio, x11,
# wayland client. Dropping cups in particular keeps libcups2 (and the rest)
# off the image; libfreerdp then needs only openssl/alsa/libusb, all present.
PACKAGECONFIG = "server"
