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
    unsigned id_material;
    ACustomer customer;
    AOrderList orderList;

    Task(unsigned id_material, const ACustomer &customer, const AOrderList &orderList) : id_material(id_material),
                                                                                         customer(customer),
                                                                                         orderList(orderList) {}
};

struct PriceListWrapper {
    AProducer prod;
    APriceList priceList;
    bool end;
    bool knownSolution;

    PriceListWrapper(const AProducer &prod, const APriceList &priceList) : prod(prod), priceList(priceList),
                                                                           end(false), knownSolution(false) {}

    PriceListWrapper() : end(false), knownSolution(false) {}

    PriceListWrapper(unsigned material_id, bool knownSolution) : end(false), knownSolution(knownSolution) {
        priceList = make_shared<CPriceList>(material_id);
    }

    PriceListWrapper(bool end) : end(end), knownSolution(false) {}

};

struct BestPriceList {
    unsigned id_material;
    int pricingsLeft;
    set<AProducer> visited;
    map<unsigned, map<unsigned, double>> best;
    mutex lock;

    BestPriceList() = default;

    BestPriceList(unsigned id_material, int pricingsLeft) : id_material(id_material), pricingsLeft(pricingsLeft) {}

    void updateBestPriceList(const PriceListWrapper &priceListWrapper) {
        auto it = visited.find(priceListWrapper.prod);
        if (it == visited.end()) {
            visited.emplace_hint(it, priceListWrapper.prod);
            pricingsLeft--;

            for (const auto &e : priceListWrapper.priceList->m_List) {
                auto firstMap = best.find(e.m_W);
                if (firstMap == best.end()) {
                    best[e.m_W][e.m_H] = e.m_Cost;
                } else {
                    auto &secondMap = firstMap->second;
                    auto it = secondMap.find(e.m_H);
                    if (it == secondMap.end()) {
                        firstMap->second[e.m_H] = e.m_Cost;
                    } else {
                        if (it->second > e.m_Cost) {
                            it->second = e.m_Cost;
                        }
                    }
                }
            }
        }
    }

    APriceList createGetPriceList() {
        APriceList priceList = make_shared<CPriceList>(id_material);
        for (auto &i: best) {
            for (auto &j: i.second) {
                priceList->Add(CProd(i.first, j.first, j.second));
            }
        }
        return priceList;
    }

    BestPriceList &operator=(const BestPriceList &other) {
        if (this != &other) {
            id_material = other.id_material;
            pricingsLeft = other.pricingsLeft;
            best = other.best;
        }
        return *this;
    }

    BestPriceList(const BestPriceList &other) {
        id_material = other.id_material;
        pricingsLeft = other.pricingsLeft;
        best = other.best;
    }
};

struct ToSolve {
    BestPriceList *best;
    vector<Task> tasks;

    ToSolve() {}

    void solve() {
        if (!tasks.empty()) {
            APriceList priceList = best->createGetPriceList();
            for (auto &t : tasks) {
                ProgtestSolver(t.orderList->m_List, priceList);
                t.customer->Completed(t.orderList);
            }
        }
    }
};

class CWeldingCompany {
private:
    atomic<int> activeCustomerCnt;
    int workerThreadCount;

    map<int, vector<Task>> taskPool;
    deque<PriceListWrapper> priceListPool;
    map<int, BestPriceList> bestPriceList;
    set<int> obtainedOrders;
    set<int> knownSolutions;

    vector<AProducer> producers;
    vector<ACustomer> customers;

    vector<thread> threadsCustomer;
    vector<thread> threadsWorker;

    mutex mtx_taskPoolManip;
    mutex mtx_priceListPoolManip;
    mutex mtx_bestPriceListPoolManip;
    mutex mtx_obtainedOrdersManip;
    mutex mtx_knownSolutionsManip;

    condition_variable cv_priceListPoolEmpty; // protects from removing from empty pool
    condition_variable cv_taskPoolEmpty; // signals when TaskPool is empty

    //================================================================================
    void customerRoutine(ACustomer customer);

    void workerRoutine();

    PriceListWrapper popFromPricelist();

    void preprocessPricelist(PriceListWrapper &priceListWrapper);

    map<int, BestPriceList>::iterator findCreateBestPriceList(PriceListWrapper &priceListWrapper);

    void updateBestPriceList(PriceListWrapper &priceListWrapper, BestPriceList &best, ToSolve &toSolve);

    void createUpdateTask(ACustomer customer, AOrderList orderList);

    void notifyAllProducersToSendPriceLists(unsigned int material_id);

    void customerThreadEndRoutine();

    void lastCustomerThreadRoutine();

    void taskCompleted(BestPriceList &best, ToSolve &toSolve);

    void getOrdersFromCustomers(unsigned material_id);

    void addDummyPricelistForKnownSolution(unsigned material_id);

public:

    void AddProducer(AProducer prod);

    void AddCustomer(ACustomer cust);

    void AddPriceList(AProducer prod, APriceList priceList);

    void Start(unsigned thrCount);

    void Stop();

    static void SeqSolve(APriceList priceList, COrder &order);
};

void CWeldingCompany::customerRoutine(ACustomer customer) {
    AOrderList orderList;
    while (true) {
        orderList = customer->WaitForDemand();
        if (orderList == nullptr || orderList.get() == nullptr)
            break;
        createUpdateTask(customer, orderList);
        getOrdersFromCustomers(orderList->m_MaterialID);
        addDummyPricelistForKnownSolution(orderList->m_MaterialID);
    }
    customerThreadEndRoutine();
}

void CWeldingCompany::getOrdersFromCustomers(unsigned material_id) {
    unique_lock<mutex> lock_obtainedOrders(mtx_obtainedOrdersManip);
    if (obtainedOrders.find(material_id) == obtainedOrders.end()) {
        obtainedOrders.insert(material_id);
        notifyAllProducersToSendPriceLists(material_id);
    }
}

void CWeldingCompany::addDummyPricelistForKnownSolution(unsigned material_id) {
    unique_lock<mutex> lock_knownSolutions(mtx_knownSolutionsManip);
    if (knownSolutions.find(material_id) != knownSolutions.end()) {
        unique_lock<mutex> lock_priceListPoolManip(mtx_priceListPoolManip);
        priceListPool.emplace_back(PriceListWrapper(material_id, true));
        cv_priceListPoolEmpty.notify_one();
    }
}

void CWeldingCompany::workerRoutine() {
    PriceListWrapper currPriceList;
    while (true) {
        ToSolve toSolve;
        currPriceList = popFromPricelist();
        if (currPriceList.end)
            break;
        preprocessPricelist(currPriceList);
        auto it_best = findCreateBestPriceList(currPriceList);
        if (currPriceList.knownSolution)
            taskCompleted(it_best->second, toSolve);
        else
            updateBestPriceList(currPriceList, it_best->second, toSolve);
        toSolve.solve();
    }
}

void CWeldingCompany::AddPriceList(AProducer prod, APriceList priceList) {
    unique_lock<mutex> ul(mtx_priceListPoolManip);
    priceListPool.emplace_back(PriceListWrapper(prod, priceList));
    cv_priceListPoolEmpty.notify_one();
}

void CWeldingCompany::AddProducer(AProducer prod) {
    producers.emplace_back(prod);
}

void CWeldingCompany::AddCustomer(ACustomer cust) {
    customers.emplace_back(cust);
}

void CWeldingCompany::Start(unsigned thrCount) {
    workerThreadCount = thrCount;
    activeCustomerCnt = (int) customers.size();
    for (auto &c : customers)
        threadsCustomer.emplace_back([=] { customerRoutine(c); });
    for (int i = 0; i < workerThreadCount; i++) {
        threadsWorker.emplace_back([=] { workerRoutine(); });
    }
}

void CWeldingCompany::Stop() {
    for (auto &t : threadsCustomer)
        t.join();
    for (auto &t: threadsWorker)
        t.join();
}

void CWeldingCompany::SeqSolve(APriceList priceList, COrder &order) {
    vector<COrder> vec;
    vec.emplace_back(order);
    ProgtestSolver(vec, std::move(priceList));
    order = vec.front();
}

PriceListWrapper CWeldingCompany::popFromPricelist() {
    PriceListWrapper poppedPriceList;
    unique_lock<mutex> lock_PriceListPool(mtx_priceListPoolManip);
    cv_priceListPoolEmpty.wait(lock_PriceListPool, [this]() { return !priceListPool.empty(); });
    poppedPriceList = priceListPool.front();
    priceListPool.pop_front();
    return poppedPriceList;
}

map<int, BestPriceList>::iterator CWeldingCompany::findCreateBestPriceList(PriceListWrapper &priceListWrapper) {
    unsigned id = priceListWrapper.priceList->m_MaterialID;
    unique_lock<mutex> lock_BestPriceListPool(mtx_bestPriceListPoolManip);
    auto it = bestPriceList.find(id);
    if (it == bestPriceList.end())
        it = bestPriceList.emplace_hint(it, id, BestPriceList(id, (int) producers.size()));
    return it;
}

void CWeldingCompany::updateBestPriceList(PriceListWrapper &priceListWrapper, BestPriceList &best, ToSolve &toSolve) {
    unique_lock<mutex> lock_BestPriceListRecord(best.lock);
    best.updateBestPriceList(priceListWrapper);
    if (best.pricingsLeft <= 0)
        taskCompleted(best, toSolve);
}

void CWeldingCompany::taskCompleted(BestPriceList &best, ToSolve &toSolve) {
    unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
    auto taskGroup = taskPool.find(best.id_material);
    if (taskGroup != taskPool.end()) {
        toSolve.best = &best;
        toSolve.tasks = taskGroup->second;
        taskPool.erase(taskGroup);
        unique_lock<mutex> lock_knownSolutions(mtx_knownSolutionsManip);
        knownSolutions.insert(best.id_material);
    }
    if (taskPool.empty()) // signal that the TaskPool is empty
        cv_taskPoolEmpty.notify_one();
}

void CWeldingCompany::notifyAllProducersToSendPriceLists(unsigned int material_id) {
    for (const auto &p : producers) {
        p->SendPriceList(material_id);
    }
}

void CWeldingCompany::customerThreadEndRoutine() {
    activeCustomerCnt--;
    if (activeCustomerCnt == 0)
        lastCustomerThreadRoutine();
}

void CWeldingCompany::createUpdateTask(ACustomer customer, AOrderList orderList) {
    unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
    auto it = taskPool.find(orderList->m_MaterialID);
    if (it == taskPool.end()) {
        taskPool.emplace_hint(it, orderList->m_MaterialID,
                              vector<Task>{Task(orderList->m_MaterialID, customer, orderList)});
    } else
        it->second.emplace_back(Task(orderList->m_MaterialID, customer, orderList));
}

void CWeldingCompany::lastCustomerThreadRoutine() {
    unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
    cv_taskPoolEmpty.wait(lock_TaskPool, [this]() { return taskPool.empty(); });
    unique_lock<mutex> lock_PriceListPool(mtx_priceListPoolManip);
    for (int i = 0; i < workerThreadCount; i++)
        priceListPool.emplace_back(PriceListWrapper(true));
    cv_priceListPoolEmpty.notify_all(); // wake up all sleeping workerThreads
}

void CWeldingCompany::preprocessPricelist(PriceListWrapper &priceListWrapper) {
    unsigned int tmp;
    for (auto &e : priceListWrapper.priceList->m_List) {
        if (e.m_W > e.m_H) {
            tmp = e.m_W;
            e.m_W = e.m_H;
            e.m_H = tmp;
        }
    }
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
