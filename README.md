sisopen - epoc and symbian file extraction utility
=======

Sisopen is a tool to list and extract the content of SIS files. SIS files are installation files used in Symbian-based smartphones like many S60 Nokia phones.

Sisopen is an ANSI-C program, mainly targetted to Linux and other Unix-like systems it can be compiled almost in every system with a C compiler having zlib. 

## Example ##

First example, list the content of a .sis file
```
$ sisopen sis/torch.sis

sis/torch.sis: SIS header detected
  application UID: 0x78F172C
  application version: 1.20

Languages
  UK English 

Files
000   !:\system\apps\Torch\Torch.aif                                        1941
001   !:\system\apps\Torch\Torch_caption.rsc                                  32
002   !:\system\apps\Torch\Torch.rsc                                          70
003   !:\system\apps\Torch\Torch.app                                        5548
```

A more interesting example showing the ability of sisopen to fully understand SIS files including conditionals:
```
$ sisopen sis/Nokia_N70_patch.SIS

sis/Nokia_N70_patch.SIS: SIS header detected
  application UID: 0x20000BB0
  application version: 1.00

Languages
  UK English 

Files
[endif]
001 f C:\DOCUME~1\m1smith\LOCALS~1\Temp\MKS0\WrongDevice0.txt                122
[else]
003 f C:\DOCUME~1\m1smith\LOCALS~1\Temp\MKS0\Finished0.txt                   254
004 c C:\system\temp\NokiaN70Patch1.exe                                     1552
005 f C:\DOCUME~1\m1smith\LOCALS~1\Temp\MKS0\Distribution0.txt               288
[if (0x10200f9a == MachineUID)]
```

SIS file conditionals must be read upside down, starting from the bottom of the output. This is a patch for the N70 smartphone so the conditional is if MachineUID == 0x... (the N70 UID) install the files, else show a Wrong Device message.

## Features ##

Sisopen is able to:

- list SIS file content, including conditionals, file type, installation time options
- extract files (try -x)
- the verbose mode gives a lot more information about the sis header and individual files (try -v)


## Installation ##

For compilation instructions read the README file inside the tar.gz (resp. git checkout).

## Author & Credits ##

Sisopen was written by Salvatore Sanfilippo (antirez at gmail dot com) using the documentation describing the SIS format written by Alexander Thoukydides (alex at thouky dot co dot uk) available at http://www.thoukydides.webspace.virginmedia.com/sis.html.

This git repository is a copy of the original code and text found at http://oldblog.antirez.com/page/sisopen to preserve the code for the case that "oldblog" will vanish finally.

## Contacts, Patches, ...

Please submit pull requests directly here at github.


Have fun, Salvatore Sanfilippo
(addoptions for github by Hartmut Goebel)


keywords: open symbian installation file
