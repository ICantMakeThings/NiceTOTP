Import("env")
env.AddPostAction("upload", "nrfjprog --memwr 0x10001208 --val 0x200 -f NRF52 && nrfjprog --reset -f NRF52")