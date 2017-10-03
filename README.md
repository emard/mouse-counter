# Mouse Counter

Repurpose a PC mouse (USB or PS/2) on linux to
act as pulse or rotary counter.

From linux uinput infrastructure it captures
mouse REL events and counts them. It can
count forward and backwards.

# TODO

complete udev rule to detect
plugged device and start remapping daemon

currently it detects 2 usb event devices
(mouse and keyboard)
rules need to select only keyboard

[ ] store counter to a file
    (prevent loosing the counted value)
