#include "usb_lib.h"
#include <wchar.h>
#include "hardware.h"
#include "usb_audio.h"

#include <stdbool.h>
#include <math.h>

#define ENDP_IN_NUM   2
#define ENDP_MAX_IN_SIZE 256

#define STD_DESCR_LANG 0
#define STD_DESCR_VEND 1
#define STD_DESCR_PROD 2
#define STD_DESCR_SN   3

#define USB_VID 0x16C0
#define USB_PID 0x05DF

#define USB_MIC_CHANNELS_NUMBER 2
#define USB_MIC_SAMPLE_RATE 48000 //количество сэмплов в секунду

#define SINE_SAMPLES_FOR_SOF (USB_MIC_SAMPLE_RATE * USB_MIC_CHANNELS_NUMBER / 1000)

// Samples interleaved L,R,L,R ==> actually samples/2 'time' samples.
static int16_t sine_data_pos[SINE_SAMPLES_FOR_SOF] = {0};
static int16_t sine_data_neg[SINE_SAMPLES_FOR_SOF] = {0};

static void init_sine_data(void)
{
    const int sine_samples_for_single_channel = SINE_SAMPLES_FOR_SOF / USB_MIC_CHANNELS_NUMBER;
    const float deg_step = 180.0 / sine_samples_for_single_channel;
    for (int i = 0; i != sine_samples_for_single_channel; ++i) {
        float deg = i * deg_step;
        float rad = deg * 3.1415 / 180.0;
        float d = sin(rad) * 8196;
        sine_data_pos[i*2] = d;
        sine_data_pos[i*2+1] = d;
        sine_data_neg[i*2] = -d;
        sine_data_neg[i*2+1] = -d;
    }
}


static const uint8_t USB_DeviceDescriptor[] = {
  ARRLEN1(
  bLENGTH,     // bLength
  USB_DESCR_DEVICE,   // bDescriptorType - Device descriptor
  USB_U16(0x0110), // bcdUSB
  0,   // bDevice Class
  0,   // bDevice SubClass
  0,   // bDevice Protocol
  USB_EP0_BUFSZ,   // bMaxPacketSize0
  USB_U16( USB_VID ), // idVendor
  USB_U16( USB_PID ), // idProduct
  USB_U16( 1 ), // bcdDevice_Ver
  STD_DESCR_VEND,   // iManufacturer
  STD_DESCR_PROD,   // iProduct
  STD_DESCR_SN,   // iSerialNumber
  1    // bNumConfigurations
  )
};

static const uint8_t USB_DeviceQualifierDescriptor[] = {
  ARRLEN1(
  bLENGTH,     //bLength
  USB_DESCR_QUALIFIER,   // bDescriptorType - Device qualifier
  USB_U16(0x0200), // bcdUSB
  0,   // bDeviceClass
  0,   // bDeviceSubClass
  0,   // bDeviceProtocol
  USB_EP0_BUFSZ,   // bMaxPacketSize0
  1,   // bNumConfigurations
  0x00    // Reserved
  )
};

static const uint8_t USB_ConfigDescriptor[] = {
  ARRLEN34(
  ARRLEN1(
    bLENGTH, // bLength: Configuration Descriptor size
    USB_DESCR_CONFIG,    //bDescriptorType: Configuration
    wTOTALLENGTH, //wTotalLength
    2, // bNumInterfaces
    1, // bConfigurationValue: Configuration value
    0, // iConfiguration: Index of string descriptor describing the configuration
    0x80, // bmAttributes: bus powered
    0x32, // MaxPower 100 mA
  )
  ARRLEN1(//0: Audio control Interface
    bLENGTH, // bLength
    USB_DESCR_INTERFACE, // bDescriptorType
    0, // bInterfaceNumber
    0, // bAlternateSetting
    0, // bNumEndpoints (если испольуется Interrupt endpoint, может быть 1)
    USB_CLASS_AUDIO, // bInterfaceClass: 
    USB_SUBCLASS_AUDIOCONTROL, // bInterfaceSubClass: 
    0x00, // bInterfaceProtocol: 
    0x00, // iInterface
  )
    ARRLEN67(//AC interface
      ARRLEN1(//AC interface header
        bLENGTH, //bLength
        USB_DESCR_CS_INTERFACE, //bDescriptorType
        1, //bDescriptorSubType
        USB_U16(0x0100), //bcdADC //AudioDeviceClass серийный номер
        wTOTALLENGTH, //wTotalLength
        1, //bInCollection //количество интерфейсов в коллекции
        1, //bInterfaceNr(1), //массив (список) номеров интерфейсов в коллекции
        1,////bInterfaceNr(2), ...
      )
      ARRLEN1(//1. AC Input terminal
        bLENGTH, //bLength
        USB_DESCR_CS_INTERFACE, //bDescriptorType
        USBAUDIO_IF_TERM_IN, //bDescriptorSubType
        1, //bTerminalID
        USB_U16( USBAUDIO_TERMINAL_MIC ),//USB_U16( USBAUDIO_TERMINAL_MIC ), //wTerminalTypeЧто это вообще такое (а вариантов немало!)
        0, //bAssocTerminal привязка выходного терминала для создания пары. Не используем
        2, //bNrChannels
        USB_U16( 0 ), //wChannelConfig //к чему именно подключены каналы
        0, //iChannelNames
        0, //iTerminal
      )
      ARRLEN1(//2. AC Feature Unit
          bLENGTH, //bLength
          USB_DESCR_CS_INTERFACE, //bDescriptorType
          USBAUDIO_IF_FEATURE, //bDescriptorSubType
          2, //UnitID
          1, //bSourceID  <---------------------------------------------
          1, //bControlSize //размер одного элемента в массиве
          //bmaControls чем именно можно управлять
          USBAUDIO_FEATURE_MUTE, //Channel(Master) - Mute
          0,
          //нужно описать оба канала?
          0, //iFeature
          )
      ARRLEN1(//3. AC Output Terminal
        bLENGTH, //bLength
        USB_DESCR_CS_INTERFACE, //bDescriptorType
        USBAUDIO_IF_TERM_OUT, //bDescriptorSubType
        3, //bTerminalID
        USB_U16( USBAUDIO_TERMINAL_USB ),//USB_U16( USBAUDIO_TERMINAL_USB ), //wTerminalType:speaker
        0, //bAssocTerminal
        2, //bSourceID  <-------------------------------------------
        0, //iTerminal
      )
    )
    
  ARRLEN1(//1 Audio Streaming Interface
    bLENGTH, //bLength
    USB_DESCR_INTERFACE, //bDescriptorType
    1, //bInterfaceNumber
    0, //bAlternateSetting
    0, //bNumEndpoints
    USB_CLASS_AUDIO, //bInterfaceClass
    USB_SUBCLASS_AUDIOSTREAMING, //bInterfaceSubClass
    0, //bInterfaceProtocol
    0, //iInterface
  )
  ARRLEN1(//1alt Audio Streaming Interface (alternative)
    bLENGTH, //bLength
    USB_DESCR_INTERFACE, //bDescriptorType
    1, //bInterfaceNumber
    1, //bAlternateSetting
    1, //bNumEndpoints
    USB_CLASS_AUDIO, //bInterfaceClass
    USB_SUBCLASS_AUDIOSTREAMING, //bInterfaceSubClass
    0, //bInterfaceProtocol
    0, //iInterface
  )
  ARRLEN1(//AS Interface
    bLENGTH, //bLength
    USB_DESCR_CS_INTERFACE, //bDescriptorType
    USBAUDIO_AS_GENERAL, //bDescriptorSubType
    3, //bTerminalLink  <----------------------------------------
    1, //bDelay //задержка, вносимая устройством (в единицах числа фреймов)
    USB_U16( USBAUDIO_FORMAT_PCM ), //wFormatTag=PCM, тип кодирования данных //TODO описать возможные типы
  )
  ARRLEN1(//AS Format Type 1
    bLENGTH, //bLength
    USB_DESCR_CS_INTERFACE, //bDescriptorType
    USBAUDIO_AS_FORMAT, //bDescriptorSubType
    1, //bFormatType
    USB_MIC_CHANNELS_NUMBER, //bNrChannels
    2, //bSubFrameSize //количество БАЙТОВ на отсчет (1-4)
    16, //bBitResolution //количество БИТОВ на отсчет (<= bSubFrameSize*8) //наверное, то-занимаемое в потоке место, а это - реальная разрешающая способность
    1, //bSamFreqType //количество поддерживаемых частот
    USB_AC24(USB_MIC_SAMPLE_RATE), //tSamFreq //(6 байт!) массив диапазонов частот
  )
  ARRLEN1(//Endpoint descriptor
    bLENGTH, //bLength
    USB_DESCR_ENDPOINT, //bDescriptorType
    ENDP_IN_NUM | 0x80, 
    USB_ENDP_ISO, //Isochronous / Synch=none / usage=data
    USB_U16(ENDP_MAX_IN_SIZE),
    1, //bInterval - частота опроса, для изохронных всегда 1
    0, //bRefresh - хз что это, сказано выставить в 0
    0, //bSynchAddress - адрес endpoint'а для синхронизации
  )
  ARRLEN1(//Isochronous endpoint descriptor
    bLENGTH, //bLength
    USB_DESCR_ENDP_ISO, //bDescriptorType
    1, //bDescriptorSubType
    0, //bmAttributes
    0, //bLockDelayUnits (undefned)
    USB_U16(0), //wLockDelay
  )
  )
};

USB_STRING(USB_StringLangDescriptor, u"\x0409"); //lang US
USB_STRING(USB_StringManufacturingDescriptor, u"COKPOWEHEU"); //Vendor
USB_STRING(USB_StringProdDescriptor, u"USB Audio"); //Product
USB_STRING(USB_StringSerialDescriptor, u"1"); //Serial (BCD)

void usb_class_get_std_descr(uint16_t descr, const void **data, uint16_t *size){
  switch(descr & 0xFF00){
    case DEVICE_DESCRIPTOR:
      *data = &USB_DeviceDescriptor;
      *size = sizeof(USB_DeviceDescriptor);
      break;
    case CONFIGURATION_DESCRIPTOR:
      *data = &USB_ConfigDescriptor;
      *size = sizeof(USB_ConfigDescriptor);
      break;
    case DEVICE_QUALIFIER_DESCRIPTOR:
      *data = &USB_DeviceQualifierDescriptor;
      *size = USB_DeviceQualifierDescriptor[0];
      break;
    case STRING_DESCRIPTOR:
      switch(descr & 0xFF){
        case STD_DESCR_LANG:
          *data = &USB_StringLangDescriptor;
          break;
        case STD_DESCR_VEND:
          *data = &USB_StringManufacturingDescriptor;
          break;
        case STD_DESCR_PROD:
          *data = &USB_StringProdDescriptor;
          break;
        case STD_DESCR_SN:
          *data = &USB_StringSerialDescriptor;
          break;
        default:
          return;
      }
      *size = ((uint8_t*)*data)[0]; //data->bLength
      break;
    default:
      break;
  }
}

uint8_t interface[2] = {0,0};

char usb_class_ep0_in(config_pack_t *req, void **data, uint16_t *size){
  
  return 0;
}

char usb_class_ep0_out(config_pack_t *req, uint16_t offset, uint16_t rx_size){
  if(req->bmRequestType == 0x01){
    if(req->bRequest == 0x0B){
      interface[req->wIndex] = req->wValue;
      usb_ep_write(0, NULL, 0);
      return 1;
    }
  }
  return 0;
}

void data_in_callback(uint8_t epnum){
    static uint8_t toggled = false;
    const uint8_t *buf = (toggled) ? sine_data_pos : sine_data_neg;
    toggled = !toggled;
    usb_ep_write_double(ENDP_IN_NUM, buf, sizeof(sine_data_pos));
}

void usb_class_init(){
  init_sine_data();

  usb_ep_init_double( ENDP_IN_NUM | 0x80, USB_ENDP_ISO, ENDP_MAX_IN_SIZE, data_in_callback);
}

void usb_class_poll(){
  //if(GPI_ON(LBTN)){GPO_OFF(RLED); GPO_OFF(GLED);}
}
