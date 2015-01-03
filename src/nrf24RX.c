/*
	This file is part of STM32F05x brushed Copter FW
	Copyright � 2014 Felix Niessen ( felix.niessen@googlemail.com )

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.	If not, see <http://www.gnu.org/licenses/>.
*/
#include "config.h"

#define RF_CHANNEL			0x3C			// Stock TX fixed frequency
#define PAYLOADSIZE		 9

extern uint8_t failsave;
bool bind = false;
extern int16_t RXcommands[6];// Throttle,Roll,Pitch,Yaw,Aux1,Aux2
const char addr_bind[5] = {0x65, 0x65, 0x65, 0x65, 0x65};
static char addr_cmnd[5];
static char pktlength;
char rxbuffer[9] = {0,0,0,0,0,0,0,0,0};

// Configure the nrf24/Beken 2423 
void init_RFRX(){
	
	// Initialise SPI, clocks, etc.
	nrfInit();
		
	// Power up with CRC enabled
	nrfWrite1Reg(REG_CONFIG, (NRF24_EN_CRC | NRF24_PWR_UP | NRF24_PRIM_RX));	

	// Configuration
	nrfWrite1Reg(REG_EN_AA, NRF24_ENAA_PA);					// Enable auto-ack on all pipes
	nrfWrite1Reg(REG_EN_RXADDR, NRF24_ERX_PA);			// Enable all pipes
	nrfWrite1Reg(REG_SETUP_AW, NRF24_AW_5_BYTES);		// 5-byte TX/RX adddress

	nrfWrite1Reg(REG_SETUP_RETR, 0x1A);							// 500uS timeout, 10 retries
	nrfWrite1Reg(REG_RF_CH, RF_CHANNEL);						// Channel 0x3C
	nrfWrite1Reg(REG_RF_SETUP, NRF24_PWR_0dBm);		 	// 1Mbps, 0dBm
	nrfWrite1Reg(REG_STATUS, NRF_STATUS_CLEAR);		 	// Clear status

	nrfWrite1Reg( REG_RX_PW_P0, PAYLOADSIZE);	 			// Set payload size on all RX pipes
	nrfWrite1Reg( REG_RX_PW_P1, PAYLOADSIZE);
	nrfWrite1Reg( REG_RX_PW_P2, PAYLOADSIZE);
	nrfWrite1Reg( REG_RX_PW_P3, PAYLOADSIZE);
	nrfWrite1Reg( REG_RX_PW_P4, PAYLOADSIZE);
	nrfWrite1Reg( REG_RX_PW_P5, PAYLOADSIZE);

	nrfWrite1Reg( REG_FIFO_STATUS, 0x00);					 // Clear FIFO bits (unnesseary?)

	// We we need to activate feature before we do this, presubaly we can delete the next two lines
	nrfWrite1Reg( REG_DYNPD, 0x3F);									// Enable dynamic payload (all pipes)
	nrfWrite1Reg( REG_FEATURE, 0x07);								// Payloads with ACK, noack command

	 // Maybe required
	nrfRead1Reg(REG_FEATURE);
	nrfActivate();																	// Activate feature register
	nrfRead1Reg(REG_FEATURE);
	nrfWrite1Reg(REG_DYNPD, 0x3F);			 						// Enable dynamic payload length on all pipes
	nrfWrite1Reg(REG_FEATURE, 0x07);		 						// Set feature bits on

	// Check for Beken BK2421/BK2423 chip
	// It is done by using Beken specific activate code, 0x53
	// and checking that status register changed appropriately
	// There is no harm to run it on nRF24L01 because following
	// closing activate command changes state back even if it
	// does something on nRF24L01
	nrfActivateBK2423();

	if (nrfRead1Reg(REG_STATUS) & 0x80) {
			
			// RF is Beken 2423
			nrfWriteReg(0x00, (char *) "\x40\x4B\x01\xE2", 4);
			nrfWriteReg(0x01, (char *) "\xC0\x4B\x00\x00", 4);
			nrfWriteReg(0x02, (char *) "\xD0\xFC\x8C\x02", 4);
			nrfWriteReg(0x03, (char *) "\x99\x00\x39\x21", 4);
			nrfWriteReg(0x04, (char *) "\xD9\x96\x82\x1B", 4);
			nrfWriteReg(0x05, (char *) "\x24\x06\x7F\xA6", 4);
			nrfWriteReg(0x0C, (char *) "\x00\x12\x73\x00", 4);
			nrfWriteReg(0x0D, (char *) "\x46\xB4\x80\x00", 4);
			nrfWriteReg(0x04, (char *) "\xDF\x96\x82\x1B", 4);
			nrfWriteReg(0x04, (char *) "\xD9\x96\x82\x1B", 4);
	}

	nrfActivateBK2423(); // switch bank back

	// Flush the tranmit and receive buffer
	nrfFlushRx();
	nrfFlushTx();

	// Set device to bind address
	nrfWriteReg( REG_RX_ADDR_P0, (char *) addr_bind, 5);
	nrfWriteReg( REG_TX_ADDR, (char *) addr_bind, 5);

	static char status;
	
	while(!bind) {
		
		// Wait until we receive a data packet
		while(!(nrfGetStatus() & 0x40));
		
			// TX sends mutliple packets, only the last contains
			// the correct command address, so keep reading the FIFO
			// until there is no more data.
			while (!(nrfRead1Reg(REG_FIFO_STATUS) & 0x01)) { 
				
				// Bind packet is nine bytes on pipe zero
				if(nrfRxLength(0) == PAYLOADSIZE) {
					
					// Get the packet
					nrfReadRX(rxbuffer, PAYLOADSIZE);
				}
			}
			
			// Flush buffer and clear status
			nrfFlushRx();
			nrfWrite1Reg(REG_STATUS, NRF_STATUS_CLEAR);
			
				// Configure the command address
				addr_cmnd[0] = rxbuffer[0];
				addr_cmnd[1] = rxbuffer[1];
				addr_cmnd[2] = rxbuffer[2];
				addr_cmnd[3] = rxbuffer[3];
				addr_cmnd[4] = 0xC1; 		
				
				// Set to TX command address
				nrfWriteReg( REG_RX_ADDR_P0,	addr_cmnd, 5);
				nrfWriteReg( REG_TX_ADDR, addr_cmnd, 5);
				
				// Flush buffer and clear status
				nrfFlushRx();
				nrfWrite1Reg(REG_STATUS, NRF_STATUS_CLEAR);
				
				// Wait until we receive data on the command address
				while(!(nrfGetStatus() & 0x40));
				
				status = nrfGetStatus();
				pktlength = nrfRxLength(0);
				
				
				bind = true;
							
			}

}




void get_RFRXDatas()
{
		
	// If a new packet exists in the buffer
	if(nrfGetStatus() & 0x40)
	{
		// Read the latest command to the buffer
		nrfReadRX(rxbuffer, PAYLOADSIZE);
		
		// Flush the buffer and clear interrupt 
		
		nrfFlushRx();
		nrfWrite1Reg(REG_STATUS, NRF_STATUS_CLEAR);
		// FC order: T   A   E   R   A1   A2
		// RF order: T   R   E   A   Rt   Et   At   F
		
		// Bitshifting the input leads to maximum scale of 255*4 = 1020, close enough
		RXcommands[0] = constrain((((int16_t)rxbuffer[0])<<2), 0, 1000);
		RXcommands[1] = constrain((((int16_t)rxbuffer[4])<<2) - 500, -500, 500);
		RXcommands[2] = constrain((((int16_t)rxbuffer[3])<<2) - 500, -500, 500);
		RXcommands[3] = constrain((((int16_t)rxbuffer[1])<<2) - 500, -500, 500);
		RXcommands[4] = constrain((((int16_t)rxbuffer[7])<<2) - 500, -500, 500);
			
		// Since data has been received, reset failsafe counter
		failsave = 0;
	}

}


