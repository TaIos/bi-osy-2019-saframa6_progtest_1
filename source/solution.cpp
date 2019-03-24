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

    Task(int id_material, const ACustomer &customer, const AOrderList &orderList) : id_material(id_material),
                                                                                    customer(customer),
                                                                                    orderList(orderList) {}
};

struct BestPricelist {
    int id_material;
    int pricingsLeft;
    CPriceList priceList;

    BestPricelist(int id_material, int pricingsLeft, const CPriceList &priceList) : id_material(id_material),
                                                                                    pricingsLeft(pricingsLeft),
                                                                                    priceList(priceList) {}
};

struct PriceList {
    AProducer prod;
    APriceList priceList;

    PriceList(const AProducer &prod, const APriceList &priceList) : prod(prod), priceList(priceList) {}
};

class CWeldingCompany {
private:
    map<int, vector<Task>> taskPool;
    deque<PriceList> priceListPool;
    map<int, BestPricelist> bestPriceList;

    vector<AProducer> producers;
    vector<ACustomer> customers;

    vector<thread> threadsCustomer;
    vector<thread> threadsWorker;

    mutex taskPoolManip;
    mutex priceListPoolManip;

public:
    void customerRoutine(ACustomer c) {
        AOrderList orderList;

        while (true) {
            orderList = c.get()->WaitForDemand();
            if (orderList == nullptr || orderList.get() == nullptr)
                break;

            Task task(orderList.get()->m_MaterialID, c, orderList);

            taskPoolManip.lock();
            auto it = taskPool.find(task.id_material);
            if (it == taskPool.end()) {
                vector<Task> vec;
                vec.emplace_back(task);
                taskPool[task.id_material] = vec;
            } else {
                it->second.emplace_back(task);
            }
            taskPoolManip.unlock();
        }
    }

    void workerRoutine() {

    }

    static void SeqSolve(APriceList priceList, COrder &order);

    void AddProducer(AProducer prod);

    void AddCustomer(ACustomer cust);

    void AddPriceList(AProducer prod, APriceList priceList);

    void Start(unsigned thrCount);

    void Stop();
};

void CWeldingCompany::AddPriceList(AProducer prod, APriceList priceList) {
    priceListPoolManip.lock();
    priceListPool.emplace_back(PriceList(prod, priceList));
    priceListPoolManip.unlock();
}

void CWeldingCompany::AddProducer(AProducer prod) {
    producers.emplace_back(prod);
}

void CWeldingCompany::AddCustomer(ACustomer cust) {
    customers.emplace_back(cust);
}

void CWeldingCompany::Start(unsigned thrCount) {
    for (auto &c : customers)
        threadsCustomer.emplace_back([=] { customerRoutine(c); });
    for (unsigned i = 0; i < thrCount; i++)
        threadsWorker.emplace_back([=] { workerRoutine(); });
}

void CWeldingCompany::Stop() {
    for (auto &t : threadsCustomer)
        t.join();
    for (auto &t: threadsWorker)
        t.join();
}

void CWeldingCompany::SeqSolve(APriceList priceList, COrder &order) {
    vector<COrder> wrapper;
    wrapper.emplace_back(order);
    ProgtestSolver(wrapper, std::move(priceList));
}

//-------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__

int main() {
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
