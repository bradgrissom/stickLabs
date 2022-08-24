# stickLabs.io

![sticker lock](stickerLock_access_control_system.jpg?raw=true "sticker lock access system")

## An Open Source Interface for Access Control Systems

[stickLabs.io](https://www.stickLabs.io/)
manufactures **sticker locks** that unlock using NFC stickers.
Adding and removing access to the lock is a 3-step process:

Add access:
- [x] Hold up Master stick
- [x] Hold up Add stick
- [x] Hold up NFC sticker

Remove access:
- [x] Hold up Master stick
- [x] Hold up Remove stick
- [x] Hold up NFC sticker

Each **user** sticker has an associated **admin** sticker on a card.  The **admin**
card allows access to be added or removed without the **user** sticker.

## Why no patents?

The first question I get when introducing this product to people is:
> That is so cool, did you file a patent?

> No, its obvious.

### Patentability: The Nonobviousness Requirement of 35 USC 103

In summary, a patent cannot be granted if the **invention would be obvious to
a person with ordinary skill in the trade**.  The Master, Add, and Remove sticks
are simple and obvious.  It is the same concept of needing to login to an
administrative interface.

## An Open Source Interface for 3rd-party Access Control Systems

Out-of-the-box **sticker locks** from
[stickLabs.io](https://www.stickLabs.io/)
are a great solution for
interior access control for small and medium-sized businesses.  Most large
businesses want the ability to integrate these locks into their existing
access control systems.  The code in this repository allows for such
integration, including:

- Read NFC tags into a mobile app
- Write NFC admin tags
- Write NFC function tags
- Use a cloud-based or app-based database to create a **USB** NFC tag
  to paste into any lock.

## Contact Us

If you have any questions regarding integration or the **sticker lock**
products, visit the **Contact Us** section of our website at
[stickLabs.io](https://www.stickLabs.io/)
