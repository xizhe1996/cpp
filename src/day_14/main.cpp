#include <sys/epoll.h>

#include <functional>
#include <utility>

class EventLoop;

class Channel {
 public:
  Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd) {}

  int fd() const { return fd_; }

  uint32_t events() const { return events_; }
  void setRevents(uint32_t revents) { revents_ = revents; }

  bool addedToLoop() const { return addedToLoop_; }
  void setAddedToLoop(bool added) { addedToLoop_ = added; }

  void enableReading() {
    events_ |= EPOLLIN;
    update();
  }

  void enableWriting() {
    events_ |= EPOLLOUT;
    update();
  }

  void disableWriting() {
    events_ &= ~EPOLLOUT;
    update();
  }

  void disableAll() {
    events_ = 0;
    update();
  }

  void setReadCallback(std::function<void()> cb) {
    readCallback_ = std::move(cb);
  }

  void setWriteCallback(std::function<void()> cb) {
    writeCallback_ = std::move(cb);
  }

  void setCloseCallback(std::function<void()> cb) {
    closeCallback_ = std::move(cb);
  }

  void handleEvent() {
    if ((revents_ & (EPOLLHUP | EPOLLERR)) && closeCallback_) {
      closeCallback_();
      return;
    }

    if ((revents_ & EPOLLIN) && readCallback_) {
      readCallback_();
    }

    if ((revents_ & EPOLLOUT) && writeCallback_) {
      writeCallback_();
    }
  }

 private:
  void update() { loop_->updateChannel(this); }

 private:
  EventLoop* loop_;
  int fd_;
  uint32_t events_ = 0;
  uint32_t revents_ = 0;
  bool addedToLoop_ = false;

  std::function<void()> readCallback_;
  std::function<void()> writeCallback_;
  std::function<void()> closeCallback_;
};

class EventLoop {
 public:
  EventLoop();
  ~EventLoop();

  void updateChannel(Channel* channel);
  void removeChannel(Channel* channel);
  void loop();

 private:
  int epfd_;
};