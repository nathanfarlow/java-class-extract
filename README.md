
# java-class-extract

This tool efficiently extracts all java classes from a binary file. This is useful to recover classes when working with obfuscated or packed jars by extracting the classes from a jvm memory dump. First execute the jar and take a jvm memory dump with `sudo gcore -a [pid]`. Then run `./classextract [core dump] [folder to extract to]`. This tool was originally created to help solve [javaisez3](https://ctftime.org/task/16457) from redpwnCTF 2021. Currently [binwalk](https://github.com/ReFirmLabs/binwalk) identifies java classes in a binary file, but is unable to extract them since it requires some parsing.
