do_install:append() {
    local f="${D}${bindir}/tegra-flash/make-sdcard"

    # Remove sudo fallback — user must have disk group access
    sed -i '/id -u.*SUDO="sudo"/d' "$f"

    # Replace partprobe block with non-fatal version
    # Original: if ! $SUDO partprobe ... ; then ERR; exit 1; fi
    sed -i '/partprobe/,/fi/{
        /partprobe/c\    partprobe "$output" 2>/dev/null || blockdev --rereadpt "$output" 2>/dev/null || true
        /ERR: partprobe/d
        /exit 1/d
        /fi/d
    }' "$f"
}
