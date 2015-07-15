
#include <stdio.h>
#include <signal.h>


#include "minorGems/util/stringUtils.h"
#include "minorGems/util/SettingsManager.h"
#include "minorGems/util/SimpleVector.h"
#include "minorGems/network/SocketServer.h"

#include "minorGems/game/doublePair.h"


#include "map.h"



typedef struct LiveObject {
        int id;
        
        // start and dest for a move
        // same if reached destination
        int xs;
        int ys;
        
        int xd;
        int yd;
        
        double moveTotalSeconds;
        double moveStartTime;
        

        int holdingID;

        Socket *sock;
        SimpleVector<char> *sockBuffer;

        char isNew;
        char firstMessageSent;
        char error;
        char deleteSent;

        char newMove;
        
    } LiveObject;



SimpleVector<LiveObject> players;


int nextID = 0;


volatile char quit = false;

void intHandler( int inUnused ) {
    quit = true;
    }


int numConnections = 0;







// reads all waiting data from socket and stores it in buffer
void readSocketFull( Socket *inSock, SimpleVector<char> *inBuffer ) {

    char buffer[512];
    
    int numRead = inSock->receive( (unsigned char*)buffer, 512, 0 );
    
    
    while( numRead > 0 ) {
        inBuffer->appendArray( buffer, numRead );

        numRead = inSock->receive( (unsigned char*)buffer, 512, 0 );
        }    
    }



// NULL if there's no full message available
char *getNextClientMessage( SimpleVector<char> *inBuffer ) {
    // find first terminal character #

    int index = inBuffer->getElementIndex( '#' );
        
    if( index == -1 ) {
        return NULL;
        }
    
    char *message = new char[ index + 1 ];
    
    for( int i=0; i<index; i++ ) {
        message[i] = inBuffer->getElementDirect( 0 );
        inBuffer->deleteElement( 0 );
        }
    // delete message terminal character
    inBuffer->deleteElement( 0 );
    
    message[ index ] = '\0';
    
    return message;
    }





typedef enum messageType {
	MOVE,
    USE,
    GRAB,
    DROP,
    UNKNOWN
    } messageType;


typedef struct ClientMessage {
        messageType type;
        int x, y;
    } ClientMessage;




ClientMessage parseMessage( char *inMessage ) {
    
    char nameBuffer[100];
    
    ClientMessage m;
    
    int numRead = sscanf( inMessage, 
                          "%99s %d %d#", nameBuffer, &( m.x ), &( m.y ) );


    if( numRead != 3 ) {
        m.type = UNKNOWN;
        return m;
        }
    

    if( strcmp( nameBuffer, "MOVE" ) == 0) {
        m.type = MOVE;
        }
    else if( strcmp( nameBuffer, "USE" ) == 0 ) {
        m.type = USE;
        }
    else if( strcmp( nameBuffer, "GRAB" ) == 0 ) {
        m.type = GRAB;
        }
    else if( strcmp( nameBuffer, "DROP" ) == 0 ) {
        m.type = DROP;
        }
    else {
        m.type = UNKNOWN;
        }
    
    return m;
    }




// returns NULL if there are no matching moves
char *getMovesMessage( char inNewMovesOnly ) {
    
    SimpleVector<char> messageBuffer;

    messageBuffer.appendElementString( "PLAYER_MOVES_START\n" );

    int numPlayers = players.size();
                
    
    int numLines = 0;

    for( int i=0; i<numPlayers; i++ ) {
                
        LiveObject *o = players.getElement( i );
                

        if( ( o->xd != o->xs || o->yd != o->ys )
            &&
            ( o->newMove || !inNewMovesOnly ) ) {

 
            // p_id xs ys xd yd fraction_done eta_sec
            
            double deltaSec = Time::getCurrentTime() - o->moveStartTime;
            
            double etaSec = o->moveTotalSeconds - deltaSec;
                
            if( inNewMovesOnly ) {
                o->newMove = false;
                }
            
            // holding no object for now
            char *messageLine = 
                autoSprintf( "%d %d %d %d %d %f %f\n", o->id, 
                             o->xs, o->ys, o->xd, o->yd, 
                             o->moveTotalSeconds, etaSec );
                                    
            messageBuffer.appendElementString( messageLine );
            delete [] messageLine;

            numLines ++;
            
            }
        }
    
        
    if( numLines > 0 ) {
        
        messageBuffer.push_back( '#' );
                
        char *message = messageBuffer.getElementString();
        
        return message;
        }
    
    return NULL;
    
    }








int main() {

    printf( "Test server\n" );

    signal( SIGINT, intHandler );


    initMap();
    
    
    int port = 
        SettingsManager::getIntSetting( "port", 5077 );
    

    
    SocketServer server( port, 256 );

    printf( "Listening for connection on port %d\n", port );

    while( !quit ) {
        
        Socket *sock = server.acceptConnection( 0 );
        
        if( sock != NULL ) {
            printf( "Got connection\n" );
            numConnections ++;
            
            LiveObject newObject;
            newObject.id = nextID;
            nextID++;
            newObject.xs = 0;
            newObject.ys = 0;
            newObject.xd = 0;
            newObject.yd = 0;
            newObject.moveTotalSeconds = 0;
            newObject.holdingID = 0;
            newObject.sock = sock;
            newObject.sockBuffer = new SimpleVector<char>();
            newObject.isNew = true;
            newObject.firstMessageSent = false;
            newObject.error = false;
            newObject.deleteSent = false;
            newObject.newMove = false;

            players.push_back( newObject );            
            
            printf( "New player connected as player %d\n", newObject.id );

            printf( "Listening for another connection on port %d\n", port );
            }
        

        int numLive = players.size();
        

        // listen for any messages from clients 

        // track index of each player that needs an update sent about it
        // we compose the full update message below
        SimpleVector<int> playerIndicesToSendUpdatesAbout;
        
        // accumulated text of update lines
        SimpleVector<char> newUpdates;

        
        for( int i=0; i<numLive; i++ ) {
            LiveObject *nextPlayer = players.getElement( i );
            

            readSocketFull( nextPlayer->sock, nextPlayer->sockBuffer );
            
            char *message = getNextClientMessage( nextPlayer->sockBuffer );
            
            if( message != NULL ) {
                printf( "Got client message: %s\n", message );
                
                ClientMessage m = parseMessage( message );
                
                delete [] message;
                
                
                if( m.type == MOVE ) {
                    
                    if( nextPlayer->xs == nextPlayer->xd &&
                        nextPlayer->ys == nextPlayer->yd ) {
                    
                        // ignore new move if not stationary
    
                        

                        nextPlayer->xd = m.x;
                        nextPlayer->yd = m.y;
                        
                        doublePair start = { nextPlayer->xs, 
                                             nextPlayer->ys };
                        doublePair dest = { m.x, m.y };
                        
                        double dist = distance( start, dest );
                        
                        
                        // for now, all move 1 square per sec
                        
                        nextPlayer->moveTotalSeconds = dist;
                        nextPlayer->moveStartTime = Time::getCurrentTime();
                        
                        nextPlayer->newMove = true;
                        }
                    }
                }
            
                
            if( nextPlayer->isNew ) {
                // their first position is an update
                

                playerIndicesToSendUpdatesAbout.push_back( i );
                
                nextPlayer->isNew = false;
                }
            else if( nextPlayer->error && ! nextPlayer->deleteSent ) {
                char *updateLine = autoSprintf( "%d %d X X\n", 
                                                nextPlayer->id,
                                                nextPlayer->holdingID );
                
                newUpdates.appendElementString( updateLine );
                
                delete [] updateLine;
                
                nextPlayer->isNew = false;
                
                nextPlayer->deleteSent = true;
                }
            else {
                // check if they are done moving
                // if so, send an update
                

                if( nextPlayer->xd != nextPlayer->xs ||
                    nextPlayer->yd != nextPlayer->ys ) {
                
                    
                    if( Time::getCurrentTime() - nextPlayer->moveStartTime
                        >
                        nextPlayer->moveTotalSeconds ) {
                        
                        // done
                        nextPlayer->xs = nextPlayer->xd;
                        nextPlayer->ys = nextPlayer->yd;
                        nextPlayer->newMove = false;
                        

                        playerIndicesToSendUpdatesAbout.push_back( i );
                        }
                    }
                
                }
            
            
            }
        

        
        

        
        for( int i=0; i<playerIndicesToSendUpdatesAbout.size(); i++ ) {
            LiveObject *nextPlayer = players.getElement( 
                playerIndicesToSendUpdatesAbout.getElementDirect( i ) );

            char *updateLine = autoSprintf( "%d %d  %d %d\n", 
                                            nextPlayer->id,
                                            nextPlayer->holdingID,
                                            nextPlayer->xs, 
                                            nextPlayer->ys );

            newUpdates.appendElementString( updateLine );
            
            delete [] updateLine;
            }
        

        
        char *moveMessage = getMovesMessage( true );
        
        int moveMessageLength = 0;
        
        if( moveMessage != NULL ) {
            moveMessageLength = strlen( moveMessage );
            }
        
                



        char *updateMessage = NULL;
        int updateMessageLength = 0;
        
        if( newUpdates.size() > 0 ) {
            newUpdates.push_back( '#' );
            char *temp = newUpdates.getElementString();

            updateMessage = concatonate( "PLAYER_UPDATE\n", temp );
            delete [] temp;

            updateMessageLength = strlen( updateMessage );
            }
        

        
        // send moves and updates to clients
        
        for( int i=0; i<numLive; i++ ) {
            
            LiveObject *nextPlayer = players.getElement(i);
            
            
            if( ! nextPlayer->firstMessageSent ) {
                

                // first, send the map chunk around them

                char *mapChunkMessage = getChunkMessage( nextPlayer->xs,
                                                         nextPlayer->ys );
                
                
                int messageLength = strlen( mapChunkMessage );

                int numSent = 
                    nextPlayer->sock->send( (unsigned char*)mapChunkMessage, 
                                            messageLength, 
                                            false, false );
                
                delete [] mapChunkMessage;
                

                if( numSent == -1 ) {
                    nextPlayer->error = true;
                    }
                else if( numSent != messageLength ) {
                    // still not sent, try again later
                    continue;
                    }



                // now send starting message
                SimpleVector<char> messageBuffer;

                messageBuffer.appendElementString( "PLAYER_UPDATE\n" );

                int numPlayers = players.size();
            
                // must be last in message
                char *playersLine;
                
                for( int i=0; i<numPlayers; i++ ) {
                
                    LiveObject o = *( players.getElement( i ) );
                

                    // holding no object for now
                    char *messageLine = 
                        autoSprintf( "%d %d %d %d\n", o.id, o.holdingID,
                                     o.xs, o.ys );
                    

                    if( o.id != nextPlayer->id ) {
                        messageBuffer.appendElementString( messageLine );
                        delete [] messageLine;
                        }
                    else {
                        // save until end
                        playersLine = messageLine;
                        }
                    }

                messageBuffer.appendElementString( playersLine );
                delete [] playersLine;
                
                messageBuffer.push_back( '#' );
            
                char *message = messageBuffer.getElementString();
                messageLength = strlen( message );

                numSent = 
                    nextPlayer->sock->send( (unsigned char*)message, 
                                            messageLength, 
                                            false, false );
                
                delete [] message;
                

                if( numSent == -1 ) {
                    nextPlayer->error = true;
                    }
                else if( numSent != messageLength ) {
                    // still not sent, try again later
                    continue;
                    }



                char *movesMessage = getMovesMessage( false );
                
                if( movesMessage != NULL ) {
                    
                
                    messageLength = strlen( movesMessage );
                    
                    numSent = 
                        nextPlayer->sock->send( (unsigned char*)movesMessage, 
                                                messageLength, 
                                            false, false );
                    
                    delete [] movesMessage;
                    

                    if( numSent == -1 ) {
                        nextPlayer->error = true;
                        }
                    else if( numSent != messageLength ) {
                        // still not sent, try again later
                        continue;
                        }
                    }
                
                nextPlayer->firstMessageSent = true;
                }
            else {
                // this player has first message, ready for updates/moves

                if( updateMessage != NULL ) {
                    

                    int numSent = 
                        nextPlayer->sock->send( (unsigned char*)updateMessage, 
                                                updateMessageLength, 
                                                false, false );

                    if( numSent == -1 ) {
                        nextPlayer->error = true;
                        }
                    }
                if( moveMessage != NULL ) {
                    int numSent = 
                        nextPlayer->sock->send( (unsigned char*)moveMessage, 
                                                moveMessageLength, 
                                                false, false );

                    if( numSent == -1 ) {
                        nextPlayer->error = true;
                        }
                    
                    }
                
                }
            }

        if( moveMessage != NULL ) {
            delete [] moveMessage;
            }
        if( updateMessage != NULL ) {
            delete [] updateMessage;
            }
        

        
        // handle closing any that have an error
        for( int i=0; i<players.size(); i++ ) {
            LiveObject *nextPlayer = players.getElement(i);

            if( nextPlayer->error && nextPlayer->deleteSent ) {
                printf( "Closing connection to player %d on error\n",
                        nextPlayer->id );
                
                delete nextPlayer->sock;
                delete nextPlayer->sockBuffer;
                players.deleteElement( i );
                i--;
                }
            }

        }
    

    printf( "Quitting...\n" );
    

    for( int i=0; i<players.size(); i++ ) {
        LiveObject *nextPlayer = players.getElement(i);
        delete nextPlayer->sock;
        delete nextPlayer->sockBuffer;
        }
    
    freeMap();

    printf( "Done.\n" );


    return 0;
    }
