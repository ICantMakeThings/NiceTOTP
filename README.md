
# NiceTOTP

Verry niceeee ;>

<img width="200" height="300" alt="ew" src="https://github.com/user-attachments/assets/44ae206d-7d15-4607-9325-636519ae4e47" />


# What is NiceTOTP?

Time-based one-time password (TOTP). aka: 2FA!

A alternetive to [Authy](https://www.authy.com/) / [Google Authenticator](https://play.google.com/store/apps/details?id=com.google.android.apps.authenticator2). 

Full offline. And Standalone once all Keys have been added.

Sleep mode after 1 minuite. battery life info at the bottom

press a button or plug it in to charge to wake it

[Video here](https://www.youtube.com/watch?v=sLiadPXk7rc)

## But Why?? 

There are a few reasons why I made this device, mainly to lose dependence of my phone. But not just, What if your phone breaks, bricks, or something else? I rather have lots of devices that don't depend on eachother rather than a all in one for that reason, plus most "universal" stuff performs worse than a specific device for that single function. As of right now, I'd say it's almost complete (enough to daily drive), possibly a few more hardware security features, maybe UI polishing, fixing any bugs i haven't found yet and should be perfect. The cost is ~£6 excluding 3D printing.

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

# More Info / 1 Month Review
Using it for one month, The battery has been used up ~20% (750mAh supposedly)
2 month update, it still only has been used up 20%.. so ye

The RTC drifted a whoping 8 seconds in that month, I mean what?!

It still shows the right code tho, but that means the last 8 Seconds are invalid. I will need to see whats up with the RTC

Or Find a better one.

###### More info on my [Site](https://icmt.cc/p/nicetotp/)

![certification-mark-PL000020-wide](https://github.com/user-attachments/assets/abe0bc33-e4d6-4658-8217-302497127993)
