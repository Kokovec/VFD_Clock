# VFD_Clock
<h2>A VFD Clock Display - PSOC5</h2>


![IMG_4650](https://github.com/user-attachments/assets/f3be5c88-f3c3-4beb-ae60-e10e518ec862)

<h3>I found these Russian surplus Vacuum Florescent Displays on EBAY.
Many of the electronic toys I had in the 70's and early 80's had VFD diplays in them.
LEDs and LCD displays simply don't have the cool glow of a VFD.  
Of course, there's a huge nostalgia kick for me.</h3>  

![image](https://github.com/user-attachments/assets/6d9cd61b-a2c1-439b-bf9f-f02647730ac5)

<h3>I've never designed anything using a VFD so I decided to read up on them and build a clock.
This is a simple clock design with the VFD as the clock display, three buttons (brightness, clock up, clock down), and a PIR to detect if someone's in the room.
The clock's display turns off after 3 minutes when the PIR doesn't detect a person (or animal) in the vicinity. It uses an RTC board with battery backup.

I didn't write a single line of code. It was created by using GROK.  
I've used Copilot and Chat GPT in the past as well, so I decided to give GROK a go.  
AI is good for low level coding and is a real time saver, but you need to get good at formulating the prompts with enough detail to get what you want out of it.  
Also, when building an embedded design it's good to give the AI engine all the datasheets and tell it how you organized the circuits.  
It takes a little bit of time upfront to get the baseline in place so the AI engine can use that to roll back code to a known state and help you troubleshoot.  
Sometimes the AI engine will make the wrong assumptions about APIs or go off an older version of an open source library.
It's almost like working with a headstrong younger Engineer who doesn't ask questions when they fail to understand something.

At the heart of the design is a PSOC5LP development board (CY8CKIT-059) which can be purchased from Mouser for $20.00 (it used to be $10 before the COVID fiasco).</h3>  
  
![image](https://github.com/user-attachments/assets/936be44b-1d2e-4606-b4c4-67d4dd59f627)
  
<h3>The PIR board I found in one of my junk boxes. It's an older board that doesn't have sensitivity adjustments.  
  
The rest of the boards were bought off Amazon.</h3>  
  
![image](https://github.com/user-attachments/assets/8e2fd54e-7ff9-4571-bef6-a3b0aac83c6d)
  
![image](https://github.com/user-attachments/assets/c5ca54c2-c733-4d01-82b8-1b1767655074)

![image](https://github.com/user-attachments/assets/8791c792-f16d-48aa-8124-36bbcc9df6f2)

![image](https://github.com/user-attachments/assets/4cb44e61-2048-48fb-931e-8d440f6435eb)

