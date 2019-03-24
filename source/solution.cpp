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

struct BestPriceList {
    int id_material;
    int pricingsLeft;
    map<unsigned, map<unsigned, double>> best;
    mutex lock;
    CPriceList priceList;

    BestPriceList(unsigned id_material, int pricingsLeft) : id_material(id_material), pricingsLeft(pricingsLeft),
                                                            priceList(CPriceList(id_material)) {}

    BestPriceList() : priceList(CPriceList(0)) {}

    void updateBestPriceList(const vector<CProd> &list) {
        // TODO
    }


    CPriceList &createGetPriceList() {
        priceList.m_List.clear();
        for (auto i: best) {
            for (auto j: i.second) {
                priceList.Add(CProd(i.first, j.first, j.second));
            }
        }
        return priceList;
    }

    BestPriceList &operator=(const BestPriceList &other) {
        if (this != &other) {
            id_material = other.id_material;
            pricingsLeft = other.pricingsLeft;
            best = other.best;
            priceList = other.priceList;
        }
        return *this;
    }
};

struct PriceListWrapper {
    AProducer prod;
    APriceList priceList;

    PriceListWrapper(const AProducer &prod, const APriceList &priceList) : prod(prod), priceList(priceList) {}

    PriceListWrapper() {}
};

class CWeldingCompany {
private:
    map<int, vector<Task>> taskPool;
    deque<PriceListWrapper> priceListPool;
    map<int, BestPriceList> bestPriceList;

    vector<AProducer> producers;
    vector<ACustomer> customers;

    vector<thread> threadsCustomer;
    vector<thread> threadsWorker;

    mutex mtx_taskPoolManip;
    mutex mtx_priceListPoolManip;
    mutex mtx_bestPriceListPoolManip;

    condition_variable cv_priceListPoolEmpty; // protecs from removing from empty pool

public:
    void customerRoutine(ACustomer c);

    void workerRoutine();

    void AddProducer(AProducer prod);

    void AddCustomer(ACustomer cust);

    void AddPriceList(AProducer prod, APriceList priceList);

    void Start(unsigned thrCount);

    void Stop();

    static void SeqSolve(APriceList priceList, COrder &order);
};

void CWeldingCompany::customerRoutine(ACustomer c) {
    AOrderList orderList;

    while (true) {
        orderList = c.get()->WaitForDemand();
        if (orderList == nullptr || orderList.get() == nullptr)
            break;

        Task task(orderList.get()->m_MaterialID, c, orderList);

        mtx_taskPoolManip.lock();
        auto it = taskPool.find(task.id_material);
        if (it == taskPool.end()) {
            vector<Task> vec;
            vec.emplace_back(task);
            taskPool[task.id_material] = vec;
        } else {
            it->second.emplace_back(task);
        }
        mtx_taskPoolManip.unlock();
    }
}

void CWeldingCompany::workerRoutine() {
    PriceListWrapper currPriceList;

    // pop PriceList from pool if any
    unique_lock<mutex> ul(mtx_priceListPoolManip);
    cv_priceListPoolEmpty.wait(ul, [this]() { return !priceListPool.empty(); });
    currPriceList = priceListPool.front();
    priceListPool.pop_front();
    ul.unlock();

    // make CurrPriceList consistent
    unsigned int tmp;
    for (auto &e : currPriceList.priceList.get()->m_List) {
        if (e.m_W > e.m_H) {
            tmp = e.m_W;
            e.m_W = e.m_H;
            e.m_H = tmp;
        }
    }

    // Find BestPriceList for updating, construct it if record doesn't exist
    map<int, BestPriceList>::iterator it;
    int id = currPriceList.priceList.get()->m_MaterialID;
    unique_lock<mutex> ul1(mtx_bestPriceListPoolManip);
    it = bestPriceList.find(id);
    if (it == bestPriceList.end()) {
        bestPriceList[id] = BestPriceList(id, (int) producers.size());
    }
    ul1.unlock();

    // Update BestPriceList with current PriceList
    unique_lock<mutex> ul2(it->second.lock);
    it->second.updateBestPriceList(currPriceList.priceList.get()->m_List);
    // we got all pricings
    if ((--(it->second.pricingsLeft)) == 0) {
        return;
    }
    ul2.unlock();
}

void CWeldingCompany::AddPriceList(AProducer prod, APriceList priceList) {
    unique_lock<mutex> ul(mtx_priceListPoolManip);
    priceListPool.emplace_back(PriceListWrapper(prod, priceList));
    cv_priceListPoolEmpty.notify_one();
    ul.unlock();
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
