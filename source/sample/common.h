// The classes in this header define the common interface between your implementation and 
// the testing environment. Exactly the same implementation is present in the progtest's 
// testing environment. You are not supposed to modify any declaration in this file, 
// any change is likely to break the compilation.
#ifndef __common_h__22034590234652093456179823592__
#define __common_h__22034590234652093456179823592__

#include <vector>
#include <memory>
//=================================================================================================
class CProd 
{
  public:
                             CProd                         ( unsigned          w,
                                                             unsigned          h,
                                                             double            cost )
      : m_W ( w ),
        m_H ( h ),
        m_Cost ( cost )
    {
    }
    
    unsigned                 m_W;
    unsigned                 m_H;
    double                   m_Cost;
};
//=================================================================================================
class CPriceList
{
  public:
                             CPriceList                    ( unsigned          materialID )
      : m_MaterialID ( materialID )
    {
    }
    
    virtual                  ~CPriceList                   ( void ) = default;
    
    CPriceList             * Add                           ( const CProd     & x )
    {
      m_List . push_back ( x );
      return this;
    }
    
    unsigned                 m_MaterialID;
    std::vector<CProd>       m_List;
};
typedef std::shared_ptr<CPriceList>                        APriceList;
//=================================================================================================
class COrder
{
  public:
                             COrder                        ( unsigned          w,
                                                             unsigned          h,
                                                             double            weldingStrength )
      : m_W ( w ),
        m_H ( h ),
        m_WeldingStrength ( weldingStrength ),
        m_Cost ( 0 )
    {
    }
    
    unsigned                 m_W;
    unsigned                 m_H;
    double                   m_WeldingStrength;
    double                   m_Cost;
};
//=================================================================================================
class COrderList
{
  public:
                              COrderList                   ( unsigned          materialID )
      : m_MaterialID ( materialID )
    {
    }      
    
    virtual                  ~COrderList                   ( void ) = default;
    
    COrderList             * Add                           ( const COrder    & x )
    {
      m_List . push_back ( x );
      return this;
    }
    
    unsigned                 m_MaterialID;
    std::vector<COrder>      m_List;
};
typedef std::shared_ptr<COrderList>                        AOrderList;
//=================================================================================================
class CProducer : public std::enable_shared_from_this<CProducer>
{
  public:
    virtual                  ~CProducer                    ( void ) = default;
  
    virtual void             SendPriceList                 ( unsigned          materialID ) = 0;
};
typedef std::shared_ptr<CProducer>                         AProducer;
//=================================================================================================
class CCustomer : public std::enable_shared_from_this<CCustomer>
{
  public:
    virtual                  ~CCustomer                    ( void ) = default;
  
    virtual AOrderList       WaitForDemand                 ( void ) = 0;
  
    virtual void             Completed                     ( AOrderList        x ) = 0;
};
typedef std::shared_ptr<CCustomer>                         ACustomer;
//=================================================================================================

#endif /* __common_h__22034590234652093456179823592__ */
