// The classes in this header are used in the example test. You are free to 
// modify these classes, add more test cases, and add more test sets.
// These classes do not exist in the progtest's testing environment.
#ifndef __sample_tester_h__0982345234598123452345__
#define __sample_tester_h__0982345234598123452345__

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "common.h"

class CProducerSync : public CProducer
{
  public:
                             CProducerSync                 ( std::function<void(AProducer, APriceList)> receiver );
    virtual void             SendPriceList                 ( unsigned          materialID ) override;
  private:
    static std::vector<CProd> c_Prod;
    std::function<void(AProducer, APriceList)> m_Receiver;
};

class CProducerAsync : public CProducer
{
  public:
                             CProducerAsync                ( std::function<void(AProducer, APriceList)> receiver );
    virtual void             SendPriceList                 ( unsigned          materialID ) override;
    void                     Start                         ( void );
    void                     Stop                          ( void );
  private:
    static std::vector<CProd> c_Prod;
    std::function<void(AProducer, APriceList)> m_Receiver;
    std::thread              m_Thr;
    std::mutex               m_Mtx;
    std::condition_variable  m_Cond;
    unsigned                 m_Req;
    bool                     m_Stop;
    void                     prodThr                       ( void );
};
typedef std::shared_ptr<CProducerAsync>                    AProducerAsync;

class CCustomerTest : public CCustomer
{
  public:
                             CCustomerTest                 ( unsigned          count );
    virtual AOrderList       WaitForDemand                 ( void );
    virtual void             Completed                     ( AOrderList        x );
  private:
    static std::vector<std::pair<COrder, double> > c_Orders;
    unsigned                 m_Count;
};

#endif /* __sample_tester_h__0982345234598123452345__ */
