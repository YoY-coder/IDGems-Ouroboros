#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <memory>

/**
    Outils d'échange entre threads, sans verrou ni allocation côté audio.

    - Mailbox<T>  : le thread UI publie un nouvel objet immuable, le thread audio le récupère.
    - Trash       : le thread audio y dépose les objets dont il n'a plus besoin ; ils sont
                    détruits plus tard par le thread UI (jamais de libération mémoire côté audio).
    - SpscQueue<T>: file de messages simples (POD) dans un sens.
*/

template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push (const T& item) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 < 1)
            return false;
        slots[(size_t) (n1 > 0 ? s1 : s2)] = item;
        fifo.finishedWrite (1);
        return true;
    }

    bool pop (T& out) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 + n2 < 1)
            return false;
        out = slots[(size_t) (n1 > 0 ? s1 : s2)];
        fifo.finishedRead (1);
        return true;
    }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, (size_t) Capacity> slots {};
};

/** File de pointeurs partagés : le "move" d'un shared_ptr n'alloue ni ne libère. */
template <typename T, int Capacity>
class SharedPtrQueue
{
public:
    bool push (std::shared_ptr<T>&& item) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 < 1)
            return false;
        slots[(size_t) (n1 > 0 ? s1 : s2)] = std::move (item);
        fifo.finishedWrite (1);
        return true;
    }

    bool pop (std::shared_ptr<T>& out) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 + n2 < 1)
            return false;
        out = std::move (slots[(size_t) (n1 > 0 ? s1 : s2)]);
        fifo.finishedRead (1);
        return true;
    }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<std::shared_ptr<T>, (size_t) Capacity> slots {};
};

/** Poubelle différée : remplie par le thread audio, vidée par le thread UI. */
class Trash
{
public:
    template <typename T>
    void dispose (std::shared_ptr<T>&& p) noexcept
    {
        if (p == nullptr)
            return;
        std::shared_ptr<const void> v = std::move (p);
        if (! queue.push (std::move (v)))
        {
            // File pleine (ne devrait jamais arriver) : on garde l'objet en vie
            // dans une réserve fixe plutôt que de le libérer ici.
            for (auto& slot : overflow)
                if (slot == nullptr) { slot = std::move (v); return; }
        }
    }

    /** À appeler depuis le thread UI (timer). */
    void empty()
    {
        std::shared_ptr<const void> p;
        while (queue.pop (p))
            p.reset();
    }

private:
    SharedPtrQueue<const void, 4096> queue;
    std::array<std::shared_ptr<const void>, 64> overflow;
};

/** Boîte aux lettres : le dernier objet publié remplace le précédent côté audio. */
template <typename T>
class Mailbox
{
public:
    /** Thread UI. */
    void post (std::shared_ptr<const T> obj) { queue.push (std::move (obj)); }

    /** Thread audio : met à jour `current` si un nouvel objet est arrivé. */
    bool fetch (std::shared_ptr<const T>& current, Trash& trash) noexcept
    {
        bool changed = false;
        std::shared_ptr<const T> incoming;
        while (queue.pop (incoming))
        {
            trash.dispose (std::move (current));
            current = std::move (incoming);
            changed = true;
        }
        return changed;
    }

private:
    SharedPtrQueue<const T, 64> queue;
};
