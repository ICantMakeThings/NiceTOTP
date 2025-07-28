# NiceTOTP

Verry niceeee ;>

<img width="200" height="300" alt="ew" src="https://github.com/user-attachments/assets/44ae206d-7d15-4607-9325-636519ae4e47" />


## What is NiceTOTP?

Time-based one-time password (TOTP). aka: 2FA!

A alternetive to [Authy](https://www.authy.com/) / [Google Authenticator](https://play.google.com/store/apps/details?id=com.google.android.apps.authenticator2). 

Full offline. And Standalone once all Keys have been added.

Battery life for now seems like itll last, sleep mode after 1 minuite.

press a button or plug it in to charge to wake it

## Hardware is:
+ Nice!Nano: [AliExpress Link](https://s.click.aliexpress.com/e/_omlmCuu)
+ DS3231 RTC: [AliExpress Link](https://s.click.aliexpress.com/e/_omVV4ia)
+ 0.96Inch Display: [AliExpress Link](https://s.click.aliexpress.com/e/_ooXwYgq)
+ 6*6 Silicone Switch: [AliExpress Link](https://s.click.aliexpress.com/e/_oDcs8Wa)
+ 3D [model](https://www.thingiverse.com/thing:7087241)

*Note: These are referral links. If you purchase through it, I earn a commission at no extra cost to you.*

![image](https://github.com/user-attachments/assets/e60bd7d0-8f01-4dfb-97a4-499b21477dde)


# Usage
Commands:
- `setunixtime` example: `setunixtime 1751925355` 
- `add <username> <base32secret>` example: `add test JBSWY3DPEHPK3PXP` ([Compare](https://totp.danhersam.com/?secret=JBSWY3DPEHPK3PXP))
- `list`
- `del <GetTheIDFromListCommand>` example: `del 1`
- `factoryreset` (Power cycle after)
More.
Or: NiceTOTP-Configurator! (Firmware update doesnt work rn*)

# Installation
+ Build and flash the project with platformio (Make sure you add nicenano support [here](https://github.com/ICantMakeThings/Nicenano-NRF52-Supermini-PlatformIO-Support))
+ Or Drag and drop the .UF2 onto the nicenano drive when doubble clicking reset (short rst pin with usbc sheild tapping twice quickly)

More info [Here](https://icmt.cc/p/nicetotp/)

## But Why?? 

There are a few reasons why I made this device, mainly to lose dependence of my phone. But not just, What if your phone breaks, bricks, or something else? I rather have lots of devices that don't depend on eachother rather than a all in one for that reason, plus most "universal" stuff performs worse than a specific device for that single function. As of right now, I'd say it's almost complete (enough to daily drive), possibly a few more hardware security features, maybe UI polishing, fixing any bugs i haven't found yet and should be perfect. The cost is ~£6 excluding 3D printing.
