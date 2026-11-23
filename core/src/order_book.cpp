#include "stocksim/order_book.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>

namespace stocksim {

OrderBook::OrderBook(std::size_t expected_orders) {
    m_nodes.reserve(expected_orders);
    m_free.reserve(expected_orders);
    m_index.reserve(expected_orders);
}

OrderBook::LevelMap& OrderBook::levels(Side side) noexcept {
    return side == Side::kBuy ? m_bids : m_asks;
}

const OrderBook::LevelMap& OrderBook::levels(Side side) const noexcept {
    return side == Side::kBuy ? m_bids : m_asks;
}

const OrderBook::Level* OrderBook::best_level(Side side) const {
    const LevelMap& map = levels(side);
    if (map.empty()) {
        return nullptr;
    }
    return side == Side::kBuy ? &std::prev(map.end())->second : &map.begin()->second;
}

std::uint32_t OrderBook::allocate(const RestingOrder& order) {
    if (!m_free.empty()) {
        const std::uint32_t index = m_free.back();
        m_free.pop_back();
        m_nodes[index] = Node{.order = order};
        return index;
    }
    assert(m_nodes.size() < kNil);
    m_nodes.push_back(Node{.order = order});
    return static_cast<std::uint32_t>(m_nodes.size() - 1);
}

bool OrderBook::add(const RestingOrder& order) {
    if (order.remaining <= Qty{0} || m_index.contains(order.id)) {
        return false;
    }
    const std::uint32_t index = allocate(order);
    Level& level = levels(order.side)[order.price];
    Node& node = m_nodes[index];
    node.prev = level.tail;
    if (level.tail != kNil) {
        m_nodes[level.tail].next = index;
    } else {
        level.head = index;
    }
    level.tail = index;
    level.total += order.remaining;
    ++level.count;
    (order.side == Side::kBuy ? m_bid_total : m_ask_total) += order.remaining;
    m_index.emplace(order.id, index);
    return true;
}

void OrderBook::erase_node(std::uint32_t index) {
    const Node node = m_nodes[index];
    LevelMap& map = levels(node.order.side);
    const auto level_it = map.find(node.order.price);
    assert(level_it != map.end());
    Level& level = level_it->second;

    if (node.prev != kNil) {
        m_nodes[node.prev].next = node.next;
    } else {
        level.head = node.next;
    }
    if (node.next != kNil) {
        m_nodes[node.next].prev = node.prev;
    } else {
        level.tail = node.prev;
    }
    level.total -= node.order.remaining;
    --level.count;
    (node.order.side == Side::kBuy ? m_bid_total : m_ask_total) -= node.order.remaining;
    if (level.count == 0) {
        map.erase(level_it);
    }

    m_index.erase(node.order.id);
    m_nodes[index] = Node{};
    m_free.push_back(index);
}

std::optional<RestingOrder> OrderBook::remove(OrderId id) {
    const auto it = m_index.find(id);
    if (it == m_index.end()) {
        return std::nullopt;
    }
    const std::uint32_t index = it->second;
    const RestingOrder order = m_nodes[index].order;
    erase_node(index);
    return order;
}

bool OrderBook::reduce(OrderId id, Qty new_remaining) {
    const auto it = m_index.find(id);
    if (it == m_index.end()) {
        return false;
    }
    RestingOrder& order = m_nodes[it->second].order;
    if (new_remaining <= Qty{0} || new_remaining >= order.remaining) {
        return false;
    }
    const Qty delta = order.remaining - new_remaining;
    levels(order.side).at(order.price).total -= delta;
    (order.side == Side::kBuy ? m_bid_total : m_ask_total) -= delta;
    order.remaining = new_remaining;
    return true;
}

std::optional<RestingOrder> OrderBook::fill_front(Side side, Qty qty) {
    LevelMap& map = levels(side);
    if (map.empty()) {
        return std::nullopt;
    }
    Level& level = side == Side::kBuy ? std::prev(map.end())->second : map.begin()->second;
    const std::uint32_t index = level.head;
    RestingOrder& order = m_nodes[index].order;
    if (qty <= Qty{0} || qty > order.remaining) {
        return std::nullopt;
    }
    order.remaining -= qty;
    order.filled += qty;
    level.total -= qty;
    (side == Side::kBuy ? m_bid_total : m_ask_total) -= qty;
    const RestingOrder result = order;
    if (result.remaining == Qty{0}) {
        erase_node(index);
    }
    return result;
}

std::optional<RestingOrder> OrderBook::find(OrderId id) const {
    const auto it = m_index.find(id);
    if (it == m_index.end()) {
        return std::nullopt;
    }
    return m_nodes[it->second].order;
}

std::optional<RestingOrder> OrderBook::front(Side side) const {
    const Level* level = best_level(side);
    if (level == nullptr) {
        return std::nullopt;
    }
    return m_nodes[level->head].order;
}

std::optional<PriceTicks> OrderBook::best_price(Side side) const {
    const LevelMap& map = levels(side);
    if (map.empty()) {
        return std::nullopt;
    }
    return side == Side::kBuy ? std::prev(map.end())->first : map.begin()->first;
}

std::size_t OrderBook::depth(Side side, std::span<DepthLevel> out) const {
    std::size_t written = 0;
    const auto emit = [&](const auto& entry) {
        out[written++] = DepthLevel{
            .price = entry.first, .qty = entry.second.total, .order_count = entry.second.count};
    };
    const LevelMap& map = levels(side);
    if (side == Side::kBuy) {
        for (auto it = map.rbegin(); it != map.rend() && written < out.size(); ++it) {
            emit(*it);
        }
    } else {
        for (auto it = map.begin(); it != map.end() && written < out.size(); ++it) {
            emit(*it);
        }
    }
    return written;
}

Qty OrderBook::total_quantity(Side side) const noexcept {
    return side == Side::kBuy ? m_bid_total : m_ask_total;
}

std::size_t OrderBook::level_count(Side side) const noexcept {
    return levels(side).size();
}

bool OrderBook::is_crossed() const {
    const std::optional<PriceTicks> bid = best_price(Side::kBuy);
    const std::optional<PriceTicks> ask = best_price(Side::kSell);
    return bid.has_value() && ask.has_value() && *bid >= *ask;
}

} // namespace stocksim
