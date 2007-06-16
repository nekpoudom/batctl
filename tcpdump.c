/* Copyright (C) 2007 B.A.T.M.A.N. contributors:
 * Andreas Langer <a.langer@q-dsl.de>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <net/if.h>
// #include <net/if_arp.h>
#include <netpacket/packet.h>
#include <sys/ioctl.h>
#include <netinet/ether.h>
#include <time.h>
#include "battool.h"

#define ETH_P_BAT 0x0842

#define	ARPOP_REQUEST	1		/* ARP request.  */
#define	ARPOP_REPLY	2			/* ARP reply.  */
#define	ARPOP_RREQUEST	3		/* RARP request.  */
#define	ARPOP_RREPLY	4			/* RARP reply.  */
#define	ARPOP_InREQUEST	8	/* InARP request.  */
#define	ARPOP_InREPLY	9			/* InARP reply.  */
#define	ARPOP_NAK	10				/* (ATM)ARP NAK.  */

struct my_arphdr
{
	uint16_t ar_hrd; /* format of hardware address */
	uint16_t ar_pro; /* format of protocol address */
	uint8_t ar_hln; /* length of hardware address */
	uint8_t ar_pln; /* length of protocol address */
	uint16_t ar_op; /* ARP opcode (command) */

	uint8_t ar_sha[ETH_ALEN]; /* sender hardware address */
	uint8_t ar_sip[4]; /* sender IP address */
	uint8_t ar_tha[ETH_ALEN]; /* target hardware address */
	uint8_t ar_tip[4]; /* target IP address */
};

void tcpdump_usage() {
	printf("Battool module tcpdump\n");
	printf("Usage: battool tcpdump|td [option] interface\n");
	printf("\t-p packet type\n\t\t1=batman packets\n\t\t2=icmp packets\n\t\t3=unicast packets\n");
	printf("\t-a all packet types\n");
	printf("\t-d packet dump in hex\n");
	printf("\t-h help\n");
	return;
}

void print_batman_packet( unsigned char *buf)
{
	struct batman_packet *bp = (struct batman_packet *)buf;
	printf("batman header:\n");
	printf(" ptype=%u flags=%x ttl=%u orig=%s seq=%u gwflags=%x version=%u\n", bp->packet_type,bp->flags, bp->ttl, ether_ntoa((struct ether_addr*) bp->orig), ntohs(bp->seqno), bp->gwflags, bp->version );
	return;
}

void print_icmp_packet( unsigned char *buf)
{
	struct icmp_packet *ip = (struct icmp_packet *)buf;
	printf("icmp header:\n");
	printf(" ptype=%u mtype=%u ttl=%u dst=%s", ip->packet_type, ip->msg_type, ip->ttl, ether_ntoa((struct ether_addr*) ip->dst ) );
	printf(" orig=%s seq=%u uid=%u\n", ether_ntoa((struct ether_addr*) ip->orig), ntohs(ip->seqno), ip->uid );
	return;
}

void print_packet( int length, unsigned char *buf )
{
	int i = 0;
	printf("\n");
	for( ; i < length; i++ ) {
		if( i == 0 )
			printf("0000| ");

		if( i != 0 && i%8 == 0 )
			printf("  ");
		if( i != 0 && i%16 == 0 )
			printf("\n%04d| ", i/16*10);

		printf("%02x ", buf[i] );
	}
	printf("\n\n");
	return;
}

void print_arp( unsigned char *buff ) {
	struct my_arphdr *arp = (struct my_arphdr*)buff;
	printf(" ARP %03u.%03u.%03u.%03u", arp->ar_sip[0], arp->ar_sip[1], arp->ar_sip[2], arp->ar_sip[3] );
	switch( ntohs( arp->ar_op ) ) {
		case ARPOP_REQUEST:
			printf(" ARP_REQUEST");
			break;
		case ARPOP_REPLY:
			printf(" ARP_REPLY");
		default:
			printf("unknown");
	}
	printf("(%u) %03u.%03u.%03u.%03u",ntohs( arp->ar_op ), arp->ar_tip[0], arp->ar_tip[1], arp->ar_tip[2], arp->ar_tip[3]);

}

int tcpdump_main( int argc, char **argv )
{
	int  optchar,
		packetsize = 2000,
		ptype=0,
		tmp;

	unsigned char packet[packetsize];
	uint8_t found_args = 1,
			print_dump = 0;

	int32_t rawsock;
	ssize_t rec_length;
	uint16_t proto = 0x0842; /* default batman packets */

	struct tm *tm;
	time_t tnow;


	struct ether_header *eth = (struct ether_header *) packet;
	struct ifreq req;
	struct sockaddr_ll addr;

	char *devicename;

	while ( ( optchar = getopt ( argc, argv, "adp:h" ) ) != -1 ) {
		switch( optchar ) {
			case 'a':
				proto = ETH_P_ALL;
				found_args+=1;
				break;
			case 'd':
    print_dump = 1;
				found_args+=1;
				break;
			case 'p':
				tmp = strtol(optarg, NULL , 10);
				if( tmp > 0 && tmp < 4 )
					ptype = tmp;
				found_args+=2;
				break;
			case 'h':
				tcpdump_usage();
				exit(EXIT_SUCCESS);
				break;
			default:
				tcpdump_usage();
				exit(EXIT_FAILURE);
		}
	}

	if ( argc <= found_args ) {
		tcpdump_usage();
		exit(EXIT_FAILURE);
	}

	devicename = argv[found_args];

	if ( ( rawsock = socket(PF_PACKET,SOCK_RAW, htons(proto) ) ) < 0 ) {
		printf("Error - can't create raw socket: %s\n", strerror(errno) );
		exit( EXIT_FAILURE );
	}

	strncpy(req.ifr_name, devicename, IFNAMSIZ);

	if ( ioctl(rawsock, SIOCGIFINDEX, &req) < 0 ) {
		printf("Error - can't create raw socket (SIOCGIFINDEX): %s\n", strerror(errno) );
		exit( EXIT_FAILURE );
	}

	addr.sll_family   = AF_PACKET;
	addr.sll_protocol = htons( proto );
	addr.sll_ifindex  = req.ifr_ifindex;

	if ( bind(rawsock, (struct sockaddr *)&addr, sizeof(addr)) < 0 ) {
		printf( "Error - can't bind raw socket: %s\n", strerror(errno) );
		close(rawsock);
		exit( EXIT_FAILURE );
	}

	while( ( rec_length = read(rawsock,packet,packetsize) ) > 0 ) {
		/* get localtime */
		time( &tnow );
		tm = localtime(&tnow);
		/* only batman packets */

		if( proto == ETH_P_ALL || ( proto == 0x0842 && ntohs(eth->ether_type) == 0x0842 ) ) {
			/* print ether header */
			printf("%02d:%02d:%02d ", tm->tm_hour, tm->tm_min, tm->tm_sec );
			printf("%s -> ",ether_ntoa( (struct ether_addr *) eth->ether_shost ) );
			printf("%s ", ether_ntoa( (struct ether_addr *)eth->ether_dhost ) );

			if( ntohs( eth->ether_type ) == ETH_P_ARP )
				print_arp( packet + sizeof(struct ether_header));
			else if( ntohs( eth->ether_type ) == ETH_P_IP )
				printf(" ip ");
			else if( ntohs( eth->ether_type ) == ETH_P_BAT )
				printf(" bat ");
			else
				printf(" %04x ",ntohs( eth->ether_type ) );

			printf(" %d bytes\n", rec_length);
		}

// 		if( !ptype || packet[sizeof( struct ether_header)] + 1 == ptype ) {


// 			printf(" %d bytes dest=%s ", rec_length, ether_ntoa( (struct ether_addr *)eth->ether_dhost ) );
// 			printf("src=%s type=%04x\n", ether_ntoa( (struct ether_addr *) eth->ether_shost ),  ntohs( eth->ether_type ) );

// 		}
/*
		if( ( !ptype && packet[sizeof( struct ether_header)] == 0 ) || ( packet[sizeof( struct ether_header)] == 0 && ptype - 1 == 0  ) ) {
			print_batman_packet( packet + sizeof( struct ether_header ) );
			print_packet( rec_length, packet );
		} else if( ( !ptype && packet[sizeof( struct ether_header)] == 1 ) || ( packet[sizeof( struct ether_header)] == 1 && ptype - 1 == 1 ) ) {
			print_icmp_packet( packet + sizeof( struct ether_header ) );
			print_packet( rec_length, packet );
		} else if( ( !ptype && packet[sizeof( struct ether_header)] == 2 ) || ( packet[sizeof( struct ether_header)] == 2 && ptype - 1 == 2 ) ) {
			printf("2 kam\n");
		} else if( ( !ptype && packet[sizeof( struct ether_header)] == 3 ) || ( packet[sizeof( struct ether_header)] == 3 && ptype - 1 == 3 ) ) {
			printf("3 kam\n");
		}
*/

		if(print_dump)
			print_packet( rec_length, packet );
	}

	exit( EXIT_SUCCESS );
}