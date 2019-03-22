#include <cstdio>
#include <cmath>
#include "sample_tester.h"
using namespace std;

//=================================================================================================
                   CProducerSync::CProducerSync            ( function<void(AProducer, APriceList)> receiver )
  : m_Receiver ( receiver )
{
}
//-------------------------------------------------------------------------------------------------
void               CProducerSync::SendPriceList            ( unsigned          materialID )
{
  if ( materialID == 1 )
  {
    APriceList l = make_shared<CPriceList> ( 1 );
    for ( const auto & x : c_Prod )
      l -> Add ( x );
    m_Receiver ( shared_from_this (), l );
  }
}
//-------------------------------------------------------------------------------------------------
vector<CProd>      CProducerSync::c_Prod =
{
  CProd ( 1, 1, 10 ),
  CProd ( 2, 7, 120 ),
  CProd ( 11, 8, 950 )
};
//=================================================================================================
                   CProducerAsync::CProducerAsync          ( function<void(AProducer, APriceList)> receiver )
  : m_Receiver ( receiver ),
    m_Req ( 0 ),
    m_Stop ( false )
{
}
//-------------------------------------------------------------------------------------------------
void               CProducerAsync::Start                   ( void )
{
  m_Thr = move ( thread ( &CProducerAsync::prodThr, this ) );
}
//-------------------------------------------------------------------------------------------------
void               CProducerAsync::Stop                    ( void )
{
  unique_lock<mutex> locker ( m_Mtx );
  m_Stop = true;
  m_Cond . notify_one ();
  locker . unlock ();
  m_Thr . join ();
}
//-------------------------------------------------------------------------------------------------
void               CProducerAsync::SendPriceList           ( unsigned          materialID )
{
  if ( materialID == 1 )
  {
    lock_guard<mutex> locker ( m_Mtx );
    m_Req ++;
    m_Cond . notify_one ();
  }  
}
//-------------------------------------------------------------------------------------------------
void               CProducerAsync::prodThr                 ( void )
{
  while ( true )
  {
    unique_lock<mutex> locker ( m_Mtx );
    m_Cond . wait ( locker, [ this ] ( void ) 
    {
      return m_Stop || m_Req > 0;
    } );
    
    if ( m_Stop )
      break;
    if ( m_Req > 0 )
    {
      m_Req --;
      locker . unlock ();
      APriceList l = make_shared<CPriceList> ( 1 );
      for ( const auto & x : c_Prod )
        l -> Add ( x );
      m_Receiver ( shared_from_this (), l );
    }
  }
}
//-------------------------------------------------------------------------------------------------
vector<CProd>      CProducerAsync::c_Prod =
{
  CProd ( 2, 7, 125 ),
  CProd ( 3, 5, 150 ),
  CProd ( 7, 3, 240 ),
  CProd ( 4, 4, 155 ),
};
//=================================================================================================
                   CCustomerTest::CCustomerTest            ( unsigned          count )
  : m_Count ( count )
{
}
//-------------------------------------------------------------------------------------------------
AOrderList         CCustomerTest::WaitForDemand            ( void )
{
  if ( ! m_Count )
    return AOrderList ();
  m_Count --;
  AOrderList req = make_shared<COrderList> ( 1 );
  for ( const auto & x : c_Orders )
    req -> Add ( x . first );
  return req;
}
//-------------------------------------------------------------------------------------------------
void               CCustomerTest::Completed                ( AOrderList        x )
{
  bool mismatch = false;
  
  for ( size_t i = 0; i < c_Orders . size (); i ++ )
    if ( fabs ( c_Orders[i] . second - x -> m_List[i] . m_Cost ) > 1e-5 * c_Orders[i] . second )
    { 
      mismatch = true;
      break;
    }
  printf ( "CCustomerTest::Completed, status = %s\n", mismatch ? "fail" : "OK" );
}
//-------------------------------------------------------------------------------------------------
vector<pair<COrder, double> > CCustomerTest::c_Orders = 
{
  make_pair ( COrder ( 2, 2, 0.0 ), 40.0 ),
  make_pair ( COrder ( 7, 12, 1.0 ), 755.0 ),
  make_pair ( COrder ( 8,  4, 10.0 ), 350.0 ),
  make_pair ( COrder ( 25, 11, 0.1 ), 2399.8 )
};
