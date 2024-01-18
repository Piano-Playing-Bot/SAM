This document collects the specifications of all protocols and formats that were created for this project.

# PIDI Format

This section specifies the format of PIDI-files. PIDI serves as a simple and memory-efficient format for storing all data required to play a song automatically on a piano. The format aims to allow microcontroller to play the music simply and performantly.

## RFCs

### Tempo

Should the format be extended to include a tempo? Currently, any time specifiers are given as a number of abstract, internal clock cycles. A tempo specification would allow explicitly setting the length of each internal clock cycle in real time.

### Memory-Efficiency

The key can only take on values between 0 and 11. It requires thus at most 4 bits, takes up 8 bits under the current format. To save space (and incidentally also improve alignment somewhat), the `on` and `key` section of a chunk could be stored at different bit-offsets in a single byte instead of being stretched over 2 seperate bytes.

The downside of this approach would be the added complexity in en-/decoding the format.

An alternative approach could be to just leave out the `on` byte, as it can be inferred. If all notes are implied to be off at the beginning, a single chunk could simply mean to toggle the given key.

## Specification

PIDI stands for `PIano Digital Interface`.

A PIDI file follows the following format:

```
<Magic Bytes: 4 bytes> <Chunks Amount: 4 bytes> <Chunks>
```

- **Magic Bytes:**
The format's Magic bytes are: `PIDI`. It is always written in big-endian format. It stands for 'Piano Digital Interface'.

- **Chunks Amount:**
The amount of chunks is given as an unsigned 32-bit number. There are exactly as many chunks as given here.

- **Chunks:**
All chunks are written here one after another. The amount of chunks is given in `Chunks Amount`. A single chunk's format is given below.

### Chunk Format

Every chunk follows the following format.

```
<Time: 8 bytes> <Speed: 2 bytes> <Key: 1 byte> <Octave: 1 byte> <On: 1 byte>
```

- **Time:**
The time specifies after how many internal clock cycles this chunk should take effect. The time of a chunk is not allowed to be lower than the time of a previous chunk. All chunks are thus ordered by their respective time.

- **Speed:**
The speed specifies the how many internal clock cycles it should take to fully start playing or to end playing the given note.

- **Key:**
The key gives the specific musical note that is referred to in this chunk. The key and octave together specify a specific key on a piano. The following keys are encoded as follows:

```
C  = 0
C# = 1
D  = 2
D# = 3
E  = 4
F  = 5
F# = 6
G  = 7
G# = 8
A  = 9
A# = 10
B  = 11
```

- **Octave:**
The octave gives the octave offset of the musical note that this chunk refers to. The key and octave together specify a specific key on a piano.

The octave should be interpreted as a signed 8-bit integer using two's complement. 0 refers to the middle octave on a piano. Negative numbers refer to octaves below the middle one, while positive numbers refer to higher octaves.

- **On:**
This byte represents whether the note should start being played or stop being played. 0 means that the note should stop being played, while any other number means that the note should start being played.


# PDIL Format

This section specifies the format of PDIL-files.

The idea behind PDIL files is to provide a way to permamently store a collection of PIDI files. The UI uses this library to provide an overview of the available songs.

## Specification

PDIL stands for `Piano Digital Interface Library`.

A PDIL file follows the following format:

```
<Magic Bytes: 4 bytes> <Amount: 4 bytes> <Song-Infos>
```

- **Magic Bytes:**
The format's Magic bytes are: `PDIL`. It is always written in big-endian format. It stands for 'Piano Digital Interface Library'.

- **Amount:**
The amount of song infos is given as an unsigned 32-bit number. There are exactly as many song infos as given here.

- **Song Infos:**
All song infos are written here one after another. The amount of song infos is given in `Amount`. A single song info's format is given below.

### Song Info Format

Every Song Info follows the following format:
```
<Name Length: 4 bytes> <Song Length: 8 bytes> <Name>
```

- **Name Length:**
The Length of the song's name. This specifies the amount of bytes that `Name` takes up.

- **Song Length:**
The length of the given song in milliseconds. This number can also be calculated dynamically from a PIDI file if required.

- **Name:**
The filename of the PIDI file, that contains the data for the given song.



# Self-Playing-Piano Protocol

This section outlines the protocol used for the communication between the UI and the microcontroller, that plays the music.

The protocol will refer to the microcontroller as the 'server' and the UI application as the 'client'. The reasoning behind this naming is as follows: The server will idly wait until it receives a message. As in general client-server architectures, the client has to initiate the communication.

When talking about either the client or the server, the term 'node' may also be used in the protocol's specification.

## RFCs

## Specification

Any message between the server and client has the following format:

```
<Magic Bytes: 4 bytes> <Message-Type: 4 bytes> <Size: 8 bytes> <Message: Size bytes>
```

- **Magic Bytes:**
The protocol's Magic bytes are: `SPPP`. It is always written in big-endian format. It stands for 'Self-Playing-Piano Protocol'.

- **Message-Type:**
The different types of messages that exist, are listed below under `Message`. Each message type has its own 4 character signature, by which it is defined. Should a node not recognize the provided message type, it should read the amount of given bytes and skip the message. Version upgrades should easily stay backward-compatible that way.

- **Size:**
The size of the message is given as an unsigned 64-bit number. This size does not include the 8 bytes of the size or the 8 bytes that came before the size.

- **Message:**
There are exactly as many bytes for the message as given in `Size`. How to interpret the message depends on the `Message-Type`. The message's format for each possible type is given below.

### 'PING'

The ping message type exists to check whether the server is available. It can also be used by the client to initially identify the port, on which the server is connected.

Only the client can send this message. The server has to respond with a `PONG` message. The message should be empty or ignored.

### 'PONG'

This message should only be sent by the server as a reply to the `PING` message. The message bytes should be empty or ignored.

### 'PIDI'

The pidi message type exists to send a [PIDI](#pidi-format) file to the server to start playing. The message should contain the entire PIDI file except for the file's initial magic bytes.

If the server is already playing some song, it should stop the previous song and start playing the new song it received in this message.

Only the client can send this message. The server has to respond with a `SUCC` message.

### 'SUCC'

The succ message is only sent by the server as a response to the client. It indicates successfully receiving and executing the client's last message. The message bytes should be empty or ignored.

### 'STOP'

The stop message is only sent by the client. It tells the server to stop/continue the current song. The first stop message should stop the song, while the next should continue it. The next one then would stop it again and so on. The message bytes should be empty or ignored.

The server has to respond with a 'SUCC' message.

### 'JUMP'

The jump message is only sent by the client. It tells the server to jump to a specific time in the song. The message should be exactly 8 bytes, that should be interpreted as an unsigned 64-bit integer, giving the internal clock cycle.

The server has to respond with a 'SUCC' message.

