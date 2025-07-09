# NiceTOTP

Verry niceeee ;>

![image](https://github.com/user-attachments/assets/9e1d1964-7a27-409e-9797-75acddbb5771)

## What is NiceTOTP?
Time-based one-time password (TOTP), 2FA, A alternetive to Authy / Authenticator

Hardware is:
+ Nice!Nano: [AliExpress Link](https://s.click.aliexpress.com/e/_omlmCuu)
+ DS3231 RTC: [AliExpress Link](https://s.click.aliexpress.com/e/_omVV4ia)
+ 0.96Inch Display: [AliExpress Link](https://s.click.aliexpress.com/e/_ooXwYgq)
+ 6*6 Silicone Switch: [AliExpress Link](https://s.click.aliexpress.com/e/_oDcs8Wa)
+ 3D model

*Note: Thease are referral links. If you purchase through it, I earn a commission at no extra cost to you.*

# Usage
Commands:
- `setunixtime` example: `setunixtime 1751925355` 
- `add <username> <base32secret>` example: `add test JBSWY3DPEHPK3PXP` ([Compare](https://totp.danhersam.com/?secret=JBSWY3DPEHPK3PXP))
- `list`
- `del <GetTheIDFromListCommand>` example: `del 1`
- `factoryreset` (Power cycle after)
- `lock`
More.

# Installation
+ Build and flash the project with platformio (Make sure you add nicenano support [here](https://github.com/ICantMakeThings/Nicenano-NRF52-Supermini-PlatformIO-Support))
+ Or Drag and drop the .UF2 onto the nicenano drive when doubble clicking reset (short rst pin with usbc sheild tapping twice quickly)

# Plans
maybe a app for easy configuration.

Full offline, if possible. (Kinda iffy abt the app. ill see.)
