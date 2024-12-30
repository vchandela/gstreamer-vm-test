### Commands to run programs

#### Mac
- `clang -c {fileName}.c -o {fileName}.o -I/Library/Frameworks/GStreamer.framework/Headers; /
  clang -o {fileName} {fileName}.o -L/Library/Frameworks/GStreamer.framework/Libraries -F/Library/Frameworks -framework GStreamer; /
  install_name_tool -add_rpath /Library/Frameworks ./{fileName}`
- `./{fileName}`

#### Linux (Ubuntu)
- `gcc {fileName}.c -o {fileName} `pkg-config --cflags --libs gstreamer-1.0`; ./{fileName}`

### Files
- `splitmuxsink-awss3sink-eos.c`
  - Wait for x sec before sending EOS to the pipeline.
  - It takes 20-21 sec for pipeline to stop but the remaining data in the buffer (< SEGMENT_DURATION) is sent to GCS
  - e.g SEGMENT_DURATION = 2 mins
      - x = 3 mins -> 1st file of 2 mins and 2nd file of 1:22 mins
      - x = 2:30 mins -> 1st file of 2 mins and 2nd file of 51 sec
      - x = 1:50 mins -> 1st file of 2 mins and 2nd file of 12 sec
      - x = 1:30 mins -> 1st file of 1:52 mins
      - x = 1 min -> 1st file of 1:22 mins
      - x = 30 sec -> 1st file of 51 sec
      - x = 15 sec -> 1st file of 36 sec
      - x = 5 sec -> 1st file of 26 sec
      - x = 1 sec -> 1st file of 22 sec
 
- `splitmuxsink-awss3sink-sigint.c`
  - Similar to `splitmuxsink-awss3sink-eos.c` but we are using SIGINT signal to trigger EOS instead of manually sending EOS.
  - After noticing that EOS is received after 21-22 sec, added a graceful shutdown period of 1 min.
