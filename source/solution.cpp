#include <utility>

#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <vector>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "progtest_solver.h"
#include "sample_tester.h"

using namespace std;
#endif /* __PROGTEST__ */

struct Task {
    int id_material;
    ACustomer customer;
    AOrderList orderList;
};

struct BestPricelist {
    int id_material;
    int pricingsLeft;
    CPriceList priceList;
};

class CWeldingCompany {
private:
    map<int, vector<Task>> taskPool;
    vector<APriceList> priceListPool;
    map<int, BestPricelist> bestPriceList;

    vector<AProducer> producers;
    vector<ACustomer> customers;


    pthread_attr_t attr;
    pthread_mutex_t task_pool_manip;

    void *waitingForDemandRoutine(void *customer) {
        ACustomer c = *((ACustomer *) customer);
        AOrderList orderList;

        while (true) {
            orderList = c.get()->WaitForDemand();
            if (orderList == nullptr || orderList.get() == nullptr)
                break;

            Task task;
            task.customer = c;
            task.id_material = orderList.get()->m_MaterialID;
            task.orderList = orderList;

            pthread_mutex_lock(&task_pool_manip);
            auto it = taskPool.find(task.id_material);
            if (it == taskPool.end()) {
                vector<Task> vec;
                vec.emplace_back(task);
                taskPool[task.id_material] = vec;
            } else {
                it->second.emplace_back(task);
            }
            pthread_mutex_unlock(&task_pool_manip);
        }


        return nullptr;
    }

public:
    CWeldingCompany();

    virtual ~CWeldingCompany();

    static void SeqSolve(APriceList priceList,
                         COrder &order);

    void AddProducer(AProducer prod);

    void AddCustomer(ACustomer cust);

    void AddPriceList(AProducer prod,
                      APriceList priceList);

    void Start(unsigned thrCount) {
    }

    void Stop(void);
};

void CWeldingCompany::SeqSolve(APriceList priceList, COrder &order) {
    vector<COrder> wrapper;
    wrapper.emplace_back(order);
    ProgtestSolver(wrapper, std::move(priceList));
}

void CWeldingCompany::AddProducer(AProducer prod) {
    producers.emplace_back(prod);
}

void CWeldingCompany::AddCustomer(ACustomer cust) {
    customers.emplace_back(cust);
}

CWeldingCompany::CWeldingCompany() {
    // create & init attributes
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    // init mutexes
    pthread_mutex_init(&task_pool_manip, nullptr);
}

CWeldingCompany::~CWeldingCompany() {
    // destroy mutexes
    pthread_mutex_destroy(&task_pool_manip);

    // destroy attributes
    pthread_attr_destroy(&attr);

}

// TODO: CWeldingCompany implementation goes here

//-------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__

int main(void) {
    using namespace std::placeholders;
    CWeldingCompany test;

    AProducer p1 = make_shared<CProducerSync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
    AProducerAsync p2 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
    test.AddProducer(p1);
    test.AddProducer(p2);
    test.AddCustomer(make_shared<CCustomerTest>(2));
    p2->Start();
    test.Start(3);
    test.Stop();
    p2->Stop();
    return 0;
}

#endif /* __PROGTEST__ */
