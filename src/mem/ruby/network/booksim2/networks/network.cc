// $Id$

/*
 Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this 
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*network.cpp
 *
 *This class is the basis of the entire network, it contains, all the routers
 *channels in the network, and is extended by all the network topologies
 *
 */

#include <cassert>
#include <sstream>

#include "mem/ruby/network/booksim2/booksim.hh"
#include "mem/ruby/network/booksim2/networks/network.hh"

#include "mem/ruby/network/booksim2/networks/kncube.hh"
#include "mem/ruby/network/booksim2/networks/fly.hh"
#include "mem/ruby/network/booksim2/networks/cmesh.hh"
#include "mem/ruby/network/booksim2/networks/flatfly_onchip.hh"
#include "mem/ruby/network/booksim2/networks/qtree.hh"
#include "mem/ruby/network/booksim2/networks/tree4.hh"
#include "mem/ruby/network/booksim2/networks/fattree.hh"
#include "mem/ruby/network/booksim2/networks/anynet.hh"
#include "mem/ruby/network/booksim2/networks/dragonfly.hh"
#include "mem/ruby/network/booksim2/networks/gem5net.hh"


BSNetwork::BSNetwork( const Configuration &config, const string & name ) :
  TimedModule( 0, name )
{
  _size     = -1; 
  _nodes    = -1; 
  _channels = -1;
  _classes  = config.GetInt("classes");
  /* ==== Power Gate - Begin ==== */
  _fabric_manager = config.GetInt("fabric_manager");
  string type = config.GetStr("sim_type");
  assert((_fabric_manager >= 0 && type == "rp") || _fabric_manager < 0);
  _off_cores = config.GetIntArray("off_cores");
  _off_routers = config.GetIntArray("off_routers");
  /* ==== Power Gate - End ==== */
}

BSNetwork::~BSNetwork( )
{
  for ( int r = 0; r < _size; ++r ) {
    if ( _routers[r] ) delete _routers[r];
  }
  for ( int s = 0; s < _nodes; ++s ) {
    if ( _inject[s] ) delete _inject[s];
    if ( _inject_cred[s] ) delete _inject_cred[s];
  }
  for ( int d = 0; d < _nodes; ++d ) {
    if ( _eject[d] ) delete _eject[d];
    if ( _eject_cred[d] ) delete _eject_cred[d];
  }
  for ( int c = 0; c < _channels; ++c ) {
    if ( _chan[c] ) delete _chan[c];
    if ( _chan_cred[c] ) delete _chan_cred[c];
    /* ==== Power Gate - Begin ==== */
    if ( _chan_handshake[c] ) delete _chan_handshake[c];
    /* ==== Power Gate - End ==== */
  }
}

BSNetwork * BSNetwork::New(const Configuration & config, const string & name)
{
  const string topo = config.GetStr( "topology" );
  BSNetwork * n = NULL;
  if ( topo == "torus" ) {
    KNCube::RegisterRoutingFunctions() ;
    n = new KNCube( config, name, false );
  } else if ( topo == "mesh" ) {
    KNCube::RegisterRoutingFunctions() ;
    n = new KNCube( config, name, true );
  } else if ( topo == "cmesh" ) {
    CMesh::RegisterRoutingFunctions() ;
    n = new CMesh( config, name );
  } else if ( topo == "fly" ) {
    KNFly::RegisterRoutingFunctions() ;
    n = new KNFly( config, name );
  } else if ( topo == "qtree" ) {
    QTree::RegisterRoutingFunctions() ;
    n = new QTree( config, name );
  } else if ( topo == "tree4" ) {
    Tree4::RegisterRoutingFunctions() ;
    n = new Tree4( config, name );
  } else if ( topo == "fattree" ) {
    FatTree::RegisterRoutingFunctions() ;
    n = new FatTree( config, name );
  } else if ( topo == "flatfly" ) {
    FlatFlyOnChip::RegisterRoutingFunctions() ;
    n = new FlatFlyOnChip( config, name );
  } else if ( topo == "anynet"){
    AnyNet::RegisterRoutingFunctions() ;
    n = new AnyNet(config, name);
  } else if ( topo == "dragonflynew"){
    DragonFlyNew::RegisterRoutingFunctions() ;
    n = new DragonFlyNew(config, name);
  } else if ( topo == "gem5net" ) {
    Gem5Net::RegisterRoutingFunctions();
    n = new Gem5Net(config, name);
  } else {
    cerr << "Unknown topology: " << topo << endl;
  }
  
  /*legacy code that insert random faults in the networks
   *not sure how to use this
   */
  if ( n && ( config.GetInt( "link_failures" ) > 0 ) ) {
    n->InsertRandomFaults( config );
  }
  return n;
}

void BSNetwork::_Alloc( )
{
  assert( ( _size != -1 ) && 
	  ( _nodes != -1 ) && 
	  ( _channels != -1 ) );

  _routers.resize(_size);
  gNodes = _nodes;

  /* ==== Power Gate - Begin ==== */
  // Core parking
  _core_states.resize(_size, true);
  _router_states.resize(_size, true);
  for (unsigned i = 0; i < _off_cores.size(); ++i) {
    int c_id = _off_cores[i];
    _core_states[c_id] = false;
  }
  for (unsigned i = 0; i < _off_routers.size(); ++i) {
    int r_id = _off_routers[i];
    _router_states[r_id] = false;
  }
  /* ==== Power Gate - End ==== */

  /*booksim used arrays of flits as the channels which makes have capacity of
   *one. To simulate channel latency, flitchannel class has been added
   *which are fifos with depth = channel latency and each cycle the channel
   *shifts by one
   *credit channels are the necessary counter part
   */
  _inject.resize(_nodes);
  _inject_cred.resize(_nodes);
  for ( int s = 0; s < _nodes; ++s ) {
    ostringstream name;
    name << Name() << "_fchan_ingress" << s;
    _inject[s] = new FlitChannel(this, name.str(), _classes);
    _inject[s]->SetSource(NULL, s);
    _timed_modules.push_back(_inject[s]);
    name.str("");
    name << Name() << "_cchan_ingress" << s;
    _inject_cred[s] = new CreditChannel(this, name.str());
    _timed_modules.push_back(_inject_cred[s]);
  }
  _eject.resize(_nodes);
  _eject_cred.resize(_nodes);
  for ( int d = 0; d < _nodes; ++d ) {
    ostringstream name;
    name << Name() << "_fchan_egress" << d;
    _eject[d] = new FlitChannel(this, name.str(), _classes);
    _eject[d]->SetSink(NULL, d);
    _timed_modules.push_back(_eject[d]);
    name.str("");
    name << Name() << "_cchan_egress" << d;
    _eject_cred[d] = new CreditChannel(this, name.str());
    _timed_modules.push_back(_eject_cred[d]);
  }
  _chan.resize(_channels);
  _chan_cred.resize(_channels);
  /* ==== Power Gate - Begin ==== */
  _chan_handshake.resize(_channels);
  /* ==== Power Gate - End ==== */
  for ( int c = 0; c < _channels; ++c ) {
    ostringstream name;
    name << Name() << "_fchan_" << c;
    _chan[c] = new FlitChannel(this, name.str(), _classes);
    _timed_modules.push_back(_chan[c]);
    name.str("");
    name << Name() << "_cchan_" << c;
    _chan_cred[c] = new CreditChannel(this, name.str());
    _timed_modules.push_back(_chan_cred[c]);
    /* ==== Power Gate - Begin ==== */
    name.str("");
    name << Name() << "_hchan_" << c;
    _chan_handshake[c] = new HandshakeChannel(this, name.str());
    _timed_modules.push_back(_chan_handshake[c]);
    /* ==== Power Gate - End ==== */
  }
}

void BSNetwork::ReadInputs( )
{
  for(deque<TimedModule *>::const_iterator iter = _timed_modules.begin();
      iter != _timed_modules.end();
      ++iter) {
    (*iter)->ReadInputs( );
  }
}

/* ==== Power Gate - Begin ==== */
void BSNetwork::PowerStateEvaluate( )
{
  for(deque<TimedModule *>::const_iterator iter = _timed_modules.begin();
      iter != _timed_modules.end();
      ++iter) {
    (*iter)->PowerStateEvaluate( );
  }
}
/* ==== Power Gate - End ==== */

void BSNetwork::Evaluate( )
{
  for(deque<TimedModule *>::const_iterator iter = _timed_modules.begin();
      iter != _timed_modules.end();
      ++iter) {
    (*iter)->Evaluate( );
  }
}

void BSNetwork::WriteOutputs( )
{
  for(deque<TimedModule *>::const_iterator iter = _timed_modules.begin();
      iter != _timed_modules.end();
      ++iter) {
    (*iter)->WriteOutputs( );
  }
}

// gem5 methods
bool BSNetwork::functionalRead(Packet *pkt)
{
    for (deque<TimedModule *>::const_iterator iter = _timed_modules.begin();
            iter != _timed_modules.end(); iter++) {
        if ((*iter)->functionalRead(pkt))
            return true;
    }

    return false;
}

uint32_t BSNetwork::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;

    for (deque<TimedModule *>::const_iterator iter = _timed_modules.begin();
            iter != _timed_modules.end(); iter++) {
        num_functional_writes += (*iter)->functionalWrite(pkt);
    }

    return num_functional_writes;
}

void BSNetwork::WriteFlit( Flit *f, int source )
{
  assert( ( source >= 0 ) && ( source < _nodes ) );
  _inject[source]->Send(f);
}

Flit *BSNetwork::ReadFlit( int dest )
{
  assert( ( dest >= 0 ) && ( dest < _nodes ) );
  return _eject[dest]->Receive();
}

void BSNetwork::WriteCredit( Credit *c, int dest )
{
  assert( ( dest >= 0 ) && ( dest < _nodes ) );
  _eject_cred[dest]->Send(c);
}

Credit *BSNetwork::ReadCredit( int source )
{
  assert( ( source >= 0 ) && ( source < _nodes ) );
  return _inject_cred[source]->Receive();
}

void BSNetwork::InsertRandomFaults( const Configuration &config )
{
  Error( "InsertRandomFaults not implemented for this topology!" );
}

void BSNetwork::OutChannelFault( int r, int c, bool fault )
{
  assert( ( r >= 0 ) && ( r < _size ) );
  _routers[r]->OutChannelFault( c, fault );
}

double BSNetwork::Capacity( ) const
{
  return 1.0;
}

/* this function can be heavily modified to display any information
 * neceesary of the network, by default, call display on each router
 * and display the channel utilization rate
 */
void BSNetwork::Display( ostream & os ) const
{
  for ( int r = 0; r < _size; ++r ) {
    _routers[r]->Display( os );
  }
}

void BSNetwork::DumpChannelMap( ostream & os, string const & prefix ) const
{
  os << prefix << "source_router,source_port,dest_router,dest_port" << endl;
  for(int c = 0; c < _nodes; ++c)
    os << prefix
       << "-1," 
       << _inject[c]->GetSourcePort() << ',' 
       << _inject[c]->GetSink()->GetID() << ',' 
       << _inject[c]->GetSinkPort() << endl;
  for(int c = 0; c < _channels; ++c)
    os << prefix
       << _chan[c]->GetSource()->GetID() << ',' 
       << _chan[c]->GetSourcePort() << ',' 
       << _chan[c]->GetSink()->GetID() << ',' 
       << _chan[c]->GetSinkPort() << endl;
  for(int c = 0; c < _nodes; ++c)
    os << prefix
       << _eject[c]->GetSource()->GetID() << ',' 
       << _eject[c]->GetSourcePort() << ',' 
       << "-1," 
       << _eject[c]->GetSinkPort() << endl;
}

void BSNetwork::DumpNodeMap( ostream & os, string const & prefix ) const
{
  os << prefix << "source_router,dest_router" << endl;
  for(int s = 0; s < _nodes; ++s)
    os << prefix
       << _eject[s]->GetSource()->GetID() << ','
       << _inject[s]->GetSink()->GetID() << endl;
}
