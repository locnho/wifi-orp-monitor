#
# nmake /f makefile
#

# Compiler and Linker settings
CC = cl.exe
LINK = link.exe

# Directories
OUTDIR = build

TARGET = swgtest.exe

# Compiler flags: /I for includes, /Fo for object file output path
INCLUDES = /I. /I..\orp_mon_wifi
CFLAGS = /nologo /EHsc $(INCLUDES)
LFLAGS = /nologo /OUT:$(OUTDIR)\$(TARGET)

# Define objects from different folders
OBJS = $(OUTDIR)\swgtest.obj \
       $(OUTDIR)\Time.obj \
       $(OUTDIR)\swganalyzer.obj

# Default target
all: $(OUTDIR) $(OUTDIR)\$(TARGET)

# Create directories if they don't exist
$(OUTDIR):
    @if not exist $(OUTDIR) mkdir $(OUTDIR)

# Link the executable
$(OUTDIR)\$(TARGET): $(OBJS)
    $(LINK) $(LFLAGS) $(OBJS)

$(OUTDIR)\swgtest.obj: swgtest.cpp
    $(CC) $(CFLAGS) /c swgtest.cpp /Fo$@

$(OUTDIR)\Time.obj: Time.cpp
    $(CC) $(CFLAGS) /c Time.cpp /Fo$@

$(OUTDIR)\swganalyzer.obj: ..\orp_mon_wifi\swganalyzer.cpp
    $(CC) $(CFLAGS) /c ..\orp_mon_wifi\swganalyzer.cpp /Fo$@

# Clean target
clean:
    del $(OUTDIR)\*.obj $(OUTDIR)\$(TARGET)
