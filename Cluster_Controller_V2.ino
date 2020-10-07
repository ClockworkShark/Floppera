//--|LIBRARIES|-----------------------------------------------------
  //COMMUNICATION:
    #include <Wire.h> //Serial library for I2C communication with master
    #include <LinkedList.h> //Linked list module for systematically processing commands outside of interrupt blocks
  //TIMERS (Settings are configured for Arduino Mega2560):
    #include <TimerOne.h>   //Timed interrupt library 1 (uses timer1). PWM on pins 11-12-13
    #include <TimerThree.h> //Timed interrupt library 2 (uses timer3). PWM on pins 2-3-5
    #include <TimerFour.h>  //Timed interrupt library 3 (uses timer4). PWM on pins 6-7-8
    #include <TimerFive.h>  //Timed interrupt library 4 (uses timer5). PWM on pins 44-45-46

//--|SYSTEM CONFIGURATION VARS|-------------------------------------
  //COMMUNICATION:
    //const byte slaveAddress = 16; //CLUSTER 1 ONLY: This cluster's unique slave address
    const byte slaveAddress = 17; //CLUSTER 2 ONLY: This cluster's unique slave address
  //DRIVES:
    const byte drivePosMin = 5;  //Lower limit on drive step range (min is 0)
    const byte drivePosMax = 75; //Upper limit on drive step range (max is 80)
    const float driveInitStepFreq = 200; //How fast drives move during initialization

//--|SYSTEM STATUS VARS|--------------------------------------------
  LinkedList<uint32_t> pendingCommands = LinkedList<uint32_t>(); //List containing received commands from mastercontroller awaiting execution

//--|CLASSES|-------------------------------------------------------
class Drive {
  //DRIVE OBJECT
  //Description: The smallest unit in the floppy driver hierarchy, representing a single floppy drive with independent
  //             pins for activation and directional control. Operates base-level upkeep functions, keeping track of
  //             where its own head is and when to reverse. Drive positional tracking is based
  //             off of boundaries and a skewed range it is set, which gives each drive
  //             automatic positional correction in the forward direction (if it overshoots).

  private:
   //DRIVE PINS (pins with instances specific and unique to this drive):
    byte _enablePin;    //DIGITAL. Pin controlling this drive's enable state
    byte _directionPin; //DIGITAL. Pin controlling this drive's step direction
   //DRIVE SETTINGS (variables which are set on initialization and never changed during runtime):
    byte _posRange; //Upper limit on drive step range (max is 80)
   //DRIVE PROPERTIES (variables which track object status throughout runtime):
    bool _enabled = true; //Whether or not this drive is currently enabled
    volatile bool _reversing = true; //Whether or not this drive is currently reversing
    volatile int _pos = 0; //The current position of this drive's head in track (changes during interrupts) (variable is int to account for possible underflow issues)
    
  public:
   //EXTERNAL DRIVE FUNCTIONS (methods which need to be called by external processes):
    Drive() { } //Default object constructor
    void Initialize(byte posRange, byte enablePin, byte directionPin) { //Drive setup function (should only be performed once)
     //Collect Startup Data:
      this->_posRange = posRange; //Set maximum drive range (range is 0 to this number)
      this->_enablePin = enablePin; //Set enable control pin position
      this->_directionPin = directionPin; //Set direction control pin position
     //PinMode Positions:
      pinMode(this->_enablePin, OUTPUT); digitalWrite(this->_enablePin, LOW); //Initialize enable control pin
      pinMode(this->_directionPin, OUTPUT); digitalWrite(this->_directionPin, HIGH); //Initialize direction control pin
    }
    void TrackStep() { //Tracks the progress of a single step, and reverses direction if necessary
     //Check Validity:
      if (!_enabled) { return; } //Skip step recording if drive is not enabled
     //Track Drive Position:
      if (!_reversing) { _pos++; } //If drive is advancing, increment position tracker
      else { _pos--; } //If drive is reversing, decrement position tracker
     //Compare With Range:
      if (_pos >= _posRange || _pos <= 0) { //If head has reached or moved outside of set range...
        ToggleDirection(); //Reverse drive direction
      }
    }
    void SetActive(bool on) { //Sets whether this drive is enabled (and tracking steps) or disabled (and inert)
      if (_enabled == on) { return; } //Skip if active setting is redundant
      _enabled = on; //Execute enable setting
      if (_enabled) { //If drive has just been enabled...
        digitalWrite(_enablePin, LOW); //Ground enable pin
      }
      else { //If drive has just been disabled...
        digitalWrite(_enablePin, HIGH); //Release enable pin
      }
    }
    void ToggleDirection() { //Reverses step direction of this drive
      _reversing = !_reversing; //Invert direction tracker
      if (_reversing) { //If drive is now reversing...
        digitalWrite(_directionPin, HIGH); //Release direction pin
      }
      else { //If drive is now moving forward...
        digitalWrite(_directionPin, LOW); //Ground direction pin
      }
    }
    void FindBestDirection() { //Calculates current drive position and checks which direction will give longest uninterrupted travel
      byte distanceFromEnd = _posRange - _pos; //Find drive head distance from max end of range
      if (!_reversing && distanceFromEnd < _pos || //If drive is moving forward but has more room behind it...
          _reversing && distanceFromEnd > _pos) { //If drive is moving backward but has more room in front of it...
        ToggleDirection(); //Reverse direction
      }
    }
};

class DriveBank {
  //DRIVE BANK OBJECT
  //Description: A group of drives capable of operating with one cohesive voice and with a volume resolution of
  //             five (0-4). Each cluster has four driveBanks, and each driveBank may be assigned to one
  //             channel. Multiple driveBanks can be assigned to and controlled by a single channel, which
  //             will increase volume resolution and total volume maximum in general. DriveBanks are
  //             initialized separately from Channels.

  private:
   //BANK PINS (pins which control all drives in bank):
    byte _stepPin; //DIGITAL. Pin controlling this bank's stepper motors
   //BANK CONTENTS (objects or pointers which organized as "children" to this object):
    Drive _drives[4]; //Initialize array of four drives per bank
   //BANK PROPERTIES (variables which track object status throughout runtime):
    volatile bool _stepping = false; //Whether or not this bank is currently stepping
   //INTERNAL BANK FUNCTIONS (methods which are only used internally):
    void ControlledStep(float interval) { //Executes a controlled step with given interval (used in drive positional initialization)
      digitalWrite(this->_stepPin, LOW); //Ground step control pin, initiating step
      delay(interval); //Delay for designated time
      digitalWrite(this->_stepPin, HIGH); //Release step control pin, ending step
      delay(interval); //Delay for designated time
    }
    
  public:
   //EXTERNAL BANK FUNCTIONS (methods which need to be called by external processes):
    DriveBank() { } //Default object constructor
    void Initialize(byte bankNumber, byte driveRangeMin, byte driveRangeMax) { //Bank setup function (should only be performed once) (includes delays) (bankNumber starts at 0)
     //Pin Setup (bank number is applied to pin template and pins are auto-generated in blocks):
      this->_stepPin = bankNumber + 2; //Assign position of step pin (B1 = pin2, B2 = pin3, B3 = pin4, B4 = pin5)
      pinMode(this->_stepPin, OUTPUT); digitalWrite(this->_stepPin, HIGH); //Initialize step control pin
     //Initialize Child Drives:
      byte pinPointer = 22 + (bankNumber * 8); //Find starting position of marker for tracking child drive connector locations (all constituent drive control pins are adjacent and in sequence)
      byte driveRange = driveRangeMax - driveRangeMin; //Find total range to send drives (starting at 0)
      for (byte x = 0; x < 4; x++) { //Iterate through all four child drives...
        this->_drives[x].Initialize(driveRange, pinPointer, pinPointer + 1); //Initialize child drive
        pinPointer += 2; //Increment pinPointer by two (to pin location for next pair of drives)
      }
     //Initialize Child Drive Positions (manually, using delays):
      float halfStepInterval = (1000 / driveInitStepFreq) / 2; //Calculate interval between initialization steps
      for (byte x = 0; x < 160; x++) { //Iterate for 80 steps...
        this->ControlledStep(halfStepInterval); //Execute a controlled step at given halfstep interval
      }
      for (byte x = 0; x < 4; x++) { //Iterate through all four child drives...
        this->_drives[x].ToggleDirection(); //Flip direction of each drive
      }
      delay(5); //Short pause to prevent steppers from being jarred
      for (byte x = 0; x < driveRangeMin * 2; x++) { //Iterate until drives are at the min point of their afforded ranges...
        this->ControlledStep(halfStepInterval); //Execute a controlled step at given halfstep interval
      }
      delay(5); //Another short pause
      for (byte x = 0; x < 4; x++) { //Iterate through all four child drives...
        this->_drives[x].SetActive(false); //Set each drive to default state of DISABLED
      }
    }
    void StepDrives() { //Alternates stepstate and advances steppers on all enabled child drives by one half-step
      _stepping = !_stepping; //Alternate stepstate
      if (_stepping) { //If bank is beginning a step...
        for (byte x = 0; x < 4; x++) { //Iterate through all four child drives...
          _drives[x].TrackStep(); //Update position on each enabled drive
        }
        digitalWrite(_stepPin, LOW); //Ground step signal pin
      }
      else { //If bank is ending a step...
        digitalWrite(_stepPin, HIGH); //Release step signal pin
      }
    }
    void ClearStep() { //Forces the completion of a full step if bank is mid-step, used at the end of notes
      if (_stepping) { StepDrives(); } //If bank is mid-step, immediately complete that step (always changes stepstate to false)
    }
    byte SetVolume(byte inputVolume) { //Sets this bank's volume according to input, and returns remaining volume
      byte remainingVolume = inputVolume; //Get tracker for how much volume is left to assign
      for (byte x = 0; x < 4; x++) { //Iterate through all four child drives...
        if (remainingVolume > 0) { //If bank has volume left to assign...
          _drives[x].SetActive(true); //Enable drive
          remainingVolume--; //Decrement remaining volume by one
        }
        else { //If no volume is left to assign...
          _drives[x].SetActive(false); //Disable drive
        }
      }
      return remainingVolume; //Return volume remainder
    }
    void OptimizeDriveDirection() { //Finds best direction for each drive by checking position of each individually
      for (int x = 0; x < 4; x++) { //Iterate through all four child drives...
        _drives[x].FindBestDirection(); //Find best direction for this drive to be facing
      }
    }
};

DriveBank banks[4]; //Initialize array of all four driveBanks in cluster (before channel declaration)

class Channel {
  //CHANNEL OBJECT
  //Description: A special object which does not represent any physical components, rather it is
  //             a grouping mechanism for modularizing drivebank control. Any number of
  //             drivebanks may be assigned to a single channel. Channels recieve signals from
  //             configured PWM timers, which they pass along to the drives they are in control
  //             of. 

  private:
   //CHANNEL SETTINGS (variables which are set only during channel configuration and re-configuration):
    bool _assignedBanks[4] = {false,false,false,false}; //Array marking which banks this channel is configured to control
    byte _volumeRes; //This channel's volume resolution (4-16). Equal to four times the number of banks it is controlling
   //CHANNEL PROPERTIES (variables which track object status throughout runtime):
    byte _volume = 0; //This channel's current volume (not scaled to volumeRes, range is 0 - 16)
    bool _playing = false; //Whether or not this channel is currently playing a note
    
  public:
   //EXTERNAL CHANNEL FUNCTIONS:
    Channel() { } //Default object constructor
    void Configure(bool useBanks[4]) { //Configure channel with new settings
     //Store Bank Settings:
      byte totalBanks = 0; //Initialize variable to store number of banks being assigned to this channel
      for (int x = 0; x < 4; x++) { //Iterate through list of bank assignments...
        this->_assignedBanks[x] = useBanks[x]; //Match each bank in internal marker list with given assignment
        if (useBanks[x] == true) { totalBanks++; } //Increment number of assigned banks by one for each bank assigned
      }
     //Update Volume:
      this->_volumeRes = totalBanks * 4; //Find volume resolution (number of individual drives)
      SetVolume(this->_volume); //Update newly-assigned banks to comply with channel volume
    }
    void SetVolume(byte newVolume) { //Configures assigned drivebanks to enable specified proportion of drives
      _volume = newVolume; //Record new volume setting
      byte actualVolume = map(_volume, 0, 16, 0, _volumeRes); //Scale volume to achievable range with current configuration
      for (byte x = 0; x < 4; x++) { //Iterate through list of banks...
        if (_assignedBanks[x]) { //If channel is assigned this bank...
          actualVolume = banks[x].SetVolume(actualVolume); //Assign portion of volume to lowest bank left in list, and update actual volume to reflect amount of volume accounted for
        }
      }
    }
    void NoteStart(byte volume = 16) { //Check which is executed every time a note is initiated
      if (_volume != volume) { SetVolume(volume); } //Set/Change volume of note (if necessary)
      if (_playing) { return; } //If channel is already playing a note, skip remainder of function
      _playing = true; //Mark that this channel is now playing a note
      for (int x = 0; x < 4; x++) { //Iterate through list of banks...
        if (_assignedBanks[x]) { //If channel is assigned this bank...
          banks[x].OptimizeDriveDirection(); //Find best direction for drives to start off in
        }
      }
    }
    void NoteStep() { //Executes one step on each assigned drivebank
      for (int x = 0; x < 4; x++) { //Iterate through list of banks...
        if (_assignedBanks[x]) { //If channel is assigned this bank...
          banks[x].StepDrives(); //Step drives on bank
        }
      }
    }
    void NoteEnd() { //Check which is executed every time a note is ended
      SetVolume(0); //Set bank volume to zero
      _playing = false; //Mark that this channel has now stopped playing
      for (int x = 0; x < 4; x++) { //Iterate through list of banks...
        if (_assignedBanks[x]) { //If channel is assigned this bank...
          banks[x].ClearStep(); //Clear any potential hanging steps on drivebanks
        }
      }
    }
};

Channel channels[4]; //Initialize array of all four channels in cluster
  
void setup() {
//--|OBJECT/LIBRARY INITIALIZATIONS|----------------------------------
  InitializeCommunication(); //Initialize communication setup (set slave address and attach receival interrupt)
  InitializeDriveBanks(); //Initialize drive banks (move every drive stepper to a known position)
  InitializeTimers(); //Initialize interrupt timers (attach according interrupt functions)
}

void loop() {
  ResolveCommands(); //Resolve all pending commands (collected during interrupt functions)
}

//--|CORE FUNCTIONS|--------------------------------------------------
  void InitializeCommunication() { //Initializes wire library to prepare clustercontroller for commands from mastercontroller
    Wire.begin(slaveAddress); //Begin I2C communication as designated slave address
    Wire.onReceive(ReceiveCommand); //Attach receival command to interrupt function
  }
  void InitializeDriveBanks() { //Steps all drives in cluster through positional initialization process
    for (int x = 0; x < 4; x++) { //Iterate through each driveBank in cluster...
      banks[x].Initialize(x, drivePosMin, drivePosMax); //Initialize each in appropriate position (pass positional settings through)
    }
  }
  void InitializeTimers() { //Initializes timer libraries and attaches them to their appropriate interrupt functions
    Timer1.initialize(1000000); //Initialize first timer
      Timer1.attachInterrupt(TriggerChannel1); //Attach Channel 1 interrupt
        Timer1.stop(); //Deactivate timer
    Timer3.initialize(1000000); //Initialize second timer
      Timer3.attachInterrupt(TriggerChannel2); //Attach Channel 2 interrupt
        Timer3.stop(); //Deactivate timer
    Timer4.initialize(1000000); //Initialize third timer
      Timer4.attachInterrupt(TriggerChannel3); //Attach Channel 3 interrupt
        Timer4.stop(); //Deactivate timer
    Timer5.initialize(1000000); //Initialize fourth timer
      Timer5.attachInterrupt(TriggerChannel4); //Attach Channel 4 interrupt
        Timer5.stop(); //Deactivate timer
  }

  //COMMAND SYSTEM:
   //Message Types (2bit):
    const uint8_t def_noteOn =        0; //Begins a note
    const uint8_t def_noteChange =    1; //Changes a note
    const uint8_t def_noteOff =       2; //Ends a note
    const uint8_t def_configChannel = 3; //Configures a channel
   //Channel Recipients (2bit):
    const uint8_t def_channel1 = 0; //Channel 1
    const uint8_t def_channel2 = 1; //Channel 2
    const uint8_t def_channel3 = 2; //Channel 3
    const uint8_t def_channel4 = 3; //Channel 4
    
    void ExecuteCommand(uint32_t command) { //Executes a 3-byte command line on this cluster
     //Retrieve Initial Message Data:               | bits | range | Description:
      uint8_t message = BitMask(command, 0, 1);   //| 2bit |  0-3  | The type of message which is being executed
      uint8_t channel = BitMask(command, 2, 3);   //| 2bit |  0-3  | The channel this command is being executed on
     //Deciphert Message Based on Type:
      if (message == def_noteOn || message == def_noteChange) { //NOTE ON & CHANGE COMMAND:
       //Retrieve Additional Message Data:
        uint8_t volume = BitMask(command, 4, 7);  //| 4bit |  0-15 | The volume of sent note
        uint16_t pitch = BitMask(command, 8, 23); //|16bit |0-65535| The pitch interval of sent note
       //Execute Command:
        channels[channel].NoteStart(volume + 1); //Perform noteStartCheck on given channel with given volume
        if (channel == def_channel1)      { Timer1.setPeriod(pitch); } //Set timer to given pitch interval
        else if (channel == def_channel2) { Timer3.setPeriod(pitch); } //Set timer to given pitch interval
        else if (channel == def_channel3) { Timer4.setPeriod(pitch); } //Set timer to given pitch interval
        else if (channel == def_channel4) { Timer5.setPeriod(pitch); } //Set timer to given pitch interval
        if (message == def_noteOn) { //Extra step for NOTE ON command
          if (channel == def_channel1)      { Timer1.setPeriod(pitch); Timer1.start(); } //Start timer cycling
          else if (channel == def_channel2) { Timer3.setPeriod(pitch); Timer3.start(); } //Start timer cycling
          else if (channel == def_channel3) { Timer4.setPeriod(pitch); Timer4.start(); } //Start timer cycling
          else if (channel == def_channel4) { Timer5.setPeriod(pitch); Timer5.start(); } //Start timer cycling
        }
      }
      else if (message == def_noteOff) { //NOTE OFF COMMAND:
       //Execute Command:
        if (channel == def_channel1)      { Timer1.stop(); } //Halt timer cycling
        else if (channel == def_channel2) { Timer3.stop(); } //Halt timer cycling
        else if (channel == def_channel3) { Timer4.stop(); } //Halt timer cycling
        else if (channel == def_channel4) { Timer5.stop(); } //Halt timer cycling
        channels[channel].NoteEnd(); //End note currently being played on specified channel
      }
      else if (message == def_configChannel) { //CHANNEL CONFIGURATION COMMAND:
       //Retrieve Additional Message Data:
        bool usingBanks[4] = {false, false, false, false}; //Initialize array to store bank assignments
        for (byte x = 0; x < 4; x++) { //Iterate through list of potential banks...
          uint8_t useBank = BitMask(command, x + 4, x + 4); //Scan single bit to check bank utilization
          if (useBank == 1) { usingBanks[x] = true; } //If bank is marked to be used, mark true in array
        }
       //Execute Command:
        channels[channel].Configure(usingBanks); //Configure desired channel according to given bank configuration
      }
    }
    void ResolveCommands() { //Resolves all pending commands (at time of function start), executing them in order of receival
      int commandCount = pendingCommands.size(); //Get number of commands currently waiting to be resolved
      for (int x = 0; x < commandCount; x++) { //Iterate through command list for each pending command...
        uint32_t newCommand = pendingCommands.shift(); //Get and remove first command in list
        ExecuteCommand(newCommand); //Execute command
      }
    }

//--|INTERRUPT FUNCTIONS|---------------------------------------------
  void ReceiveCommand(int numBytes) { //Event triggered by receival of command from mastercontroller
    uint32_t newCommand = 0; //Initialize variable to store command as it is read
    for (byte x = 0; x < numBytes; x++) { //Iterate through total number of bytes received...
      byte newByte = Wire.read(); //Recieve byte from mastercontroller
      newCommand = BitMake(newCommand, newByte, x * 8); //Add new byte to end of command data
    }
    pendingCommands.add(newCommand); //Add newly-constructed command to list of commands awaiting execution
  }
  void TriggerChannel1() { //Channel trigger 1. Primary voice
    channels[0].NoteStep();
  }
  void TriggerChannel2() { //Channel trigger 2. Secondary voice
    channels[1].NoteStep();
  }
  void TriggerChannel3() { //Channel trigger 3. Tertiary voice
    channels[2].NoteStep();
  }
  void TriggerChannel4() { //Channel trigger 4. Quaternary voice
    channels[3].NoteStep();
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
