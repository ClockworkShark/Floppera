//--|LIBRARIES|-----------------------------------------------------
 //COMMUNICATION:
  #include <Wire.h> //Inter-arduino I2C communication library
  #include <LinkedList.h> //Linked list module for systematically processing commands outside of interrupt blocks
  #include <ardumidi.h> //Arduino hairless midi serial bridge library, allows DAW to route midi data directly to arduino

//--|SYSTEM CONFIGURATION VARS|-------------------------------------
 //COMMUNICATION:
  const byte clusterAddress1 = 16; //I2C address of cluster 1
  const byte clusterAddress2 = 17; //I2C address of cluster 2

//--|SYSTEM STATUS VARS|--------------------------------------------
  LinkedList<MidiMessage> pendingMessages = LinkedList<MidiMessage>(); //List of recieved midi messages waiting to be passed on to clusters

//--|NOTES|---------------------------------------------------------
class Note {
  //NOTE OBJECT
  //Description: Contains all useful information about a single note, pre-compiled to save processing power
  //             during runtime.

  public:
   //EXTERNAL NOTE FUNCTIONS:
    uint16_t interval; //The interval this note is played at (in microseconds)
    Note(float frequency) { //Note object constructor
      this->interval = (uint16_t)((1000000 / frequency) + 0.5f); //Set note interval (round and cast result of calculation to 2-byte int)
    }
};

Note notes[128] = { //List of all pre-set notes for easy conversion from MIDI data to proprietary cluster command (contains all 128 notes a midi signal can request)
    //0TH OCTAVE:
     {8.18},  //C-1       | MIDI #0
     {8.66},  //C#-1/D~-1 | MIDI #1
     {9.18},  //D-1       | MIDI #2
     {9.72},  //D#-1/E~-1 | MIDI #3
     {10.3},  //E-1       | MIDI #4
     {10.91}, //F-1       | MIDI #5
     {11.56}, //F#-1/G~-1 | MIDI #6
     {12.25}, //G-1       | MIDI #7
     {12.98}, //G#-1/A~-1 | MIDI #8
     {13.75}, //A-1       | MIDI #9
     {14.57}, //A#-1/B~-1 | MIDI #10
     {15.43}, //B-1       | MIDI #11
    //1ST OCTAVE:
     {16.35}, //C0        | MIDI #12
     {17.32}, //C#0/D~0   | MIDI #13
     {18.35}, //D0        | MIDI #14
     {19.45}, //D#0/E~0   | MIDI #15
     {20.6},  //E0        | MIDI #16
     {21.83}, //F0        | MIDI #17
     {23.12}, //F#0/G~0   | MIDI #18
     {24.5},  //G0        | MIDI #19
     {25.96}, //G#0/A~0   | MIDI #20
     {27.5},  //A0        | MIDI #21
     {29.14}, //A#0/B~0   | MIDI #22
     {30.87}, //B0        | MIDI #23
    //2ND OCTAVE:
     {32.7},  //C1        | MIDI #24
     {34.65}, //C#1/D~1   | MIDI #25
     {36.71}, //D1        | MIDI #26
     {38.89}, //D#1/E~1   | MIDI #27
     {41.20}, //E1        | MIDI #28
     {43.65}, //F1        | MIDI #29
     {46.25}, //F#1/G~1   | MIDI #30
     {49},    //G1        | MIDI #31
     {51.91}, //G#1/A~1   | MIDI #32
     {55},    //A1        | MIDI #33
     {58.27}, //A#1/B~1   | MIDI #34
     {61.74}, //B1        | MIDI #35
    //3RD OCTAVE:
     {65.41},  //C2       | MIDI #36
     {69.3},   //C#2/D~2  | MIDI #37
     {73.42},  //D2       | MIDI #38
     {77.78},  //D#2/E~2  | MIDI #39
     {82.41},  //E2       | MIDI #40
     {87.31},  //F2       | MIDI #41
     {92.5},   //F#2/G~2  | MIDI #42
     {98},     //G2       | MIDI #43
     {103.83}, //G#2/A~2  | MIDI #44
     {110},    //A2       | MIDI #45
     {116.54}, //A#2/B~2  | MIDI #46
     {123.47}, //B2       | MIDI #47
    //4TH OCTAVE:
     {130.81}, //C3       | MIDI #48
     {138.59}, //C#3/D~3  | MIDI #49
     {146.83}, //D3       | MIDI #50
     {155.56}, //D#3/E~3  | MIDI #51
     {164.81}, //E3       | MIDI #52
     {174.61}, //F3       | MIDI #53
     {185},    //F#3/G~3  | MIDI #54
     {196},    //G3       | MIDI #55
     {207.65}, //G#3/A~3  | MIDI #56
     {220},    //A3       | MIDI #57
     {233.08}, //A#3/B~3  | MIDI #58
     {246.94}, //B3       | MIDI #59
    //5TH OCTAVE:
     {261.63}, //C4       | MIDI #60
     {277.18}, //C#4/D~4  | MIDI #61
     {293.66}, //D4       | MIDI #62
     {311.13}, //D#4/E~4  | MIDI #63
     {329.63}, //E4       | MIDI #64
     {349.23}, //F4       | MIDI #65
     {369.99}, //F#4/G~4  | MIDI #66
     {392},    //G4       | MIDI #67
     {415.3},  //G#4/A~4  | MIDI #68
     {440},    //A4       | MIDI #69
     {466.16}, //A#4/B~4  | MIDI #70
     {493.88}, //B4       | MIDI #71
    //6TH OCTAVE:
     {523.25}, //C5       | MIDI #72
     {554.37}, //C#5/D~5  | MIDI #73
     {587.33}, //D5       | MIDI #74
     {622.25}, //D#5/E~5  | MIDI #75
     {659.25}, //E5       | MIDI #76
     {698.46}, //F5       | MIDI #77
     {739.99}, //F#5/G~5  | MIDI #78
     {783.99}, //G5       | MIDI #79
     {830.61}, //G#5/A~5  | MIDI #80
     {880},    //A5       | MIDI #81
     {932.33}, //A#5/B~5  | MIDI #82
     {987.77}, //B5       | MIDI #83
    //7TH OCTAVE:
     {1046.5},  //C6      | MIDI #84
     {1108.73}, //C#6/D~6 | MIDI #85
     {1174.66}, //D6      | MIDI #86
     {1244.51}, //D#6/E~6 | MIDI #87
     {1318.51}, //E6      | MIDI #88
     {1396.91}, //F6      | MIDI #89
     {1479.98}, //F#6/G~6 | MIDI #90
     {1567.98}, //G6      | MIDI #91
     {1661.22}, //G#6/A~6 | MIDI #92
     {1760},    //A6      | MIDI #93
     {1864.66}, //A#6/B~6 | MIDI #94
     {1975.53}, //B6      | MIDI #95
    //8TH OCTAVE:
     {2093},    //C7      | MIDI #96
     {2217.46}, //C#7/D~7 | MIDI #97
     {2349.32}, //D7      | MIDI #98
     {2489.02}, //D#7/E~7 | MIDI #99
     {2637.02}, //E7      | MIDI #100
     {2793.83}, //F7      | MIDI #101
     {2959.96}, //F#7/G~7 | MIDI #102
     {3135.96}, //G7      | MIDI #103
     {3322.44}, //G#7/A~7 | MIDI #104
     {3520},    //A7      | MIDI #105
     {3729.31}, //A#7/B~7 | MIDI #106
     {3951.07}, //B7      | MIDI #107
    //9TH OCTAVE:
     {4186.01}, //C8      | MIDI #108
     {4434.92}, //C#8/D~8 | MIDI #109
     {4698.63}, //D8      | MIDI #110
     {4978.03}, //D#8/E~8 | MIDI #111
     {5274.04}, //E8      | MIDI #112
     {5587.65}, //F8      | MIDI #113
     {5919.91}, //F#8/G~8 | MIDI #114
     {6271.93}, //G8      | MIDI #115
     {6644.88}, //G#8/A~8 | MIDI #116
     {7040},    //A8      | MIDI #117
     {7458.62}, //A#8/B~8 | MIDI #118
     {7902.13},  //B8     | MIDI #119
    //10TH OCTAVE:
     {8372},   //C9       | MIDI #120
     {8869.8}, //C#9/D~9  | MIDI #121
     {9397.3}, //D9       | MIDI #122
     {9956.1}, //D#9/E~9  | MIDI #123
     {10548},  //E9       | MIDI #124
     {11175},  //F9       | MIDI #125
     {11840},  //F#9/G~9  | MIDI #126
     {12544},  //G9       | MIDI #127
  };

//--|COMMAND SYSTEM|---------------------------------------------------
 //Message Types (2bit):
  const uint8_t def_noteOn =        0; //Begins a note
  const uint8_t def_noteChange =    1; //Changes a note
  const uint8_t def_noteOff =       2; //Ends a note
  const uint8_t def_configChannel = 3; //Configures a channel
 //Channel Designations (2bit) (cluster-specific):
  const uint8_t def_channel1 = 0; //Channel 1
  const uint8_t def_channel2 = 1; //Channel 2
  const uint8_t def_channel3 = 2; //Channel 3
  const uint8_t def_channel4 = 3; //Channel 4
 //Testing Commands (3byte):
  const uint32_t tcm_configBanks1 = 0x13; //B00010011 Config channel 1, use drivebank 1
  const uint32_t tcm_configBanks2 = 0x27; //B00100111 Config channel 2, use drivebank 2
  const uint32_t tcm_configBanks3 = 0x4B; //B01001011 Config channel 3, use drivebank 3
  const uint32_t tcm_configBanks4 = 0x8F; //B10001111 Config channel 4, use drivebank 4
  const uint32_t tcm_testNoteOn1 = 0xEEEF0; //B00001110 11101110 11110000 NoteOn channel 1, volume 100%, 3822 microsec interval (C4)
  const uint32_t tcm_testNoteChange1 = 0xD4DB1; //B00001101 01001101 10110001 NoteChange channel 1, volume 68%, 3405 microsec interval (D4)
  const uint32_t tcm_channelOff1 = 0x2; //B0010 NoteOff channel 1

 //Command Builders (cluster-specific):
  uint32_t BuildNoteOnCommand(uint8_t channel, uint8_t volume, uint16_t pitch) { //Builds a 3-byte NOTE ON command which can be sent directly to a clustercontroller
   //Construct Status Nibble:
    uint32_t command = BitMake(def_noteOn, channel, 2); //Initialize command as combination of command definition and channel bits, in that order
   //Construct Relevant Data:
    command = BitMake(command, volume, 4); //Append volume to end of command
    command = BitMake(command, pitch, 8); //Append pitch (interval) to end of command
   //Export Data:
    return command; //Return constructed command
  }
  uint32_t BuildNoteChangeCommand(uint8_t channel, uint8_t volume, uint16_t pitch) { //Builds a 3-byte NOTE CHANGE command which can be sent directly to a clustercontroller
   //Construct Status Nibble:
    uint32_t command = BitMake(def_noteChange, channel, 2); //Initialize command as combination of command definition and channel bits, in that order
   //Construct Relevant Data:
    command = BitMake(command, volume, 4); //Append volume to end of command
    command = BitMake(command, pitch, 8); //Append pitch (interval) to end of command
   //Export Data:
    return command; //Return constructed command
  }
  uint32_t BuildNoteOffCommand(uint8_t channel) { //Builds a 3-byte NOTE OFF command which can be sent directly to a clustercontroller
   //Construct Status Nibble:
    uint32_t command = BitMake(def_noteOff, channel, 2); //Initialize command as combination of command definition and channel bits, in that order
   //Export Data:
    return command; //Return constructed command
  }
  uint32_t BuildConfigChannelCommand(uint8_t channel, bool bankAssignments[4]) { //Builds a 3-byte CHANNEL CONFIGURATION command which can be sent directly to a clustercontroller
   //Construct Status Nibble:
    uint32_t command = BitMake(def_configChannel, channel, 2); //Initialize command as combination of command definition and channel bits, in that order
   //Construct Relevant Data:
    for (int x = 0; x < 4; x++) { //Iterate through list of potential bank assignments...
      if (bankAssignments[x]) command = BitMake(command, 1, x + 4); //If bank is being assigned, mark it in command
    }
   //Export Data:
    return command; //Return constructed command
  }
 //Command Utilities:
  void SendCommand(uint32_t command, byte clusterAddress) { //Sends given 3-byte command to given cluster using I2C bus
    Wire.beginTransmission(clusterAddress); //Queue up slave address and check if it is present
    for (byte x = 0; x < 3; x++) { //Iterate through command until all three bytes have been queued up...
      byte bitMarker = x * 8; //Calculate position in command to bitmask from
      byte data = BitMask(command, bitMarker, bitMarker + 7 ); //Set data byte to according byte in command
      Wire.write(data); //Queue data byte in local buffer
    }
    Wire.endTransmission(); //Send queued bytes
  }

void setup() {
//--|OBJECT/LIBRARY INITIALIZATIONS|----------------------------------
  Serial.begin(115200); //Initialize serial communication at 9600 baud
  Wire.begin(); //Initialize I2C communication as master
  
//--|TESTING SEQUENCE|------------------------------------------------
  delay(2000); //Wait for cluster controller to initialize
  SendCommand(tcm_configBanks1, clusterAddress1); //Configure first channel in cluster 1
  SendCommand(tcm_configBanks2, clusterAddress1); //Configure second channel in cluster 1
  SendCommand(tcm_configBanks3, clusterAddress1); //Configure third channel in cluster 1
  SendCommand(tcm_configBanks4, clusterAddress1); //Configure fourth channel in cluster 1
  SendCommand(tcm_configBanks1, clusterAddress2); //Configure first channel in cluster 2
  SendCommand(tcm_configBanks2, clusterAddress2); //Configure second channel in cluster 2
  SendCommand(tcm_configBanks3, clusterAddress2); //Configure third channel in cluster 2
  SendCommand(tcm_configBanks4, clusterAddress2); //Configure fourth channel in cluster 2
}

void loop() {
  while (midi_message_available() > 0) { //If any midi messages are detected...
    MidiMessage newMessage = read_midi_message(); //Read new midi message
    if (newMessage.command != MIDI_NOTE_ON && newMessage.command != MIDI_NOTE_OFF) { continue; }
    pendingMessages.add(newMessage); //Add new midi message to list
    /*
    if (newMessage.command == MIDI_NOTE_ON) {
      SendCommand(BuildNoteOnCommand(0, 0, notes[newMessage.param1].interval), clusterAddress1);
      SendCommand(BuildNoteChangeCommand(0, map(newMessage.param2, 0, 127, 0, 15), notes[newMessage.param1].interval), clusterAddress1);
    }
    else if (newMessage.command == MIDI_NOTE_OFF) {
      SendCommand(BuildNoteOffCommand(0), clusterAddress1); //End note after half a second
    }
    */
  }

  int messageCount = pendingMessages.size();
  for (byte x = 0; x < messageCount; x++) { //While there are any pending midi messages waiting to be converted to cluster commands...
    MidiMessage message = pendingMessages.shift(); //Get and remove first message in pending list
    uint8_t channel = message.channel; //Get designated channel destination for message
    uint8_t clusterAddress = clusterAddress1; //Set default cluster to cluster 1
    if (channel > 3) { //If using Cluster 2...
      clusterAddress = clusterAddress2; //Set destination cluster address accordingly
      channel -= 4; //Localize channel number to be cluster-specific
    }
    if (message.command == MIDI_NOTE_ON) { //Midi note on command resolution process:
      uint16_t interval = notes[message.param1].interval; //Get note interval of given midi note
      uint8_t volume = 15; //map(message.param2, 0, 127, 0, 15); //Get velocity of given midi note
      SendCommand(BuildNoteOnCommand(channel, volume, interval), clusterAddress); //Send NoteOn command to drives
    }
    else if (message.command == MIDI_NOTE_OFF) { //Midi note off command resolution process:
      SendCommand(BuildNoteOffCommand(channel), clusterAddress); //Send NoteOff command to drives
    }
  }
  
}

//--|UTILITY FUNCTIONS|-----------------------------------------------
  uint32_t BitMask(uint32_t data, byte lsb, byte msb) { //Finds the bitwise value between (inclusive) the given lsb and msb
    //NOTE: Msb and Lsb are inclusive and based off of bit positions starting at zero
    uint32_t newData = data >> lsb; //Shift data to the left based on position of least significant byte
    uint32_t mask = (1 << ((msb + 1) - lsb)) - 1; //Create a mask to filter out unwanted bits to the left of expression
    return newData & mask; //Return new data with mask applied
  }
  uint32_t BitMake(uint32_t baseData, uint32_t data, byte lsb) { //Appends data into baseData at bit lsb (inclusive)
    return baseData | (data << lsb); //Append data to base at location of lsb (and return result)
  }
  float lerp(float v0, float v1, float t) { //Interpolates between value 0 and value 1 by t (pulled straight from Wikipedia)
    return (1 - t) * v0 + t * v1; //Interpolate values and return result (guarantees v = v1 when t = 1)
  }
