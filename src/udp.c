
#include "udp.h"

typedef struct SeqId SeqId;
typedef struct Packet Packet;
typedef struct Channel Channel;
typedef struct State State;

struct SeqId
{
  uint16_t idx;
  uint16_t key;
};

struct Packet
{
  SeqId src_seq;
  SeqId dst_seq;
  uint32_t enabled;
  uint8_t values[Max_NVariables];
};

#define NakShakeStep 0
#define ReqShakeStep 1
#define SynShakeStep 2
#define AckShakeStep 3

struct Channel
{
  SeqId seq;
  SeqId adj_seq;
  struct sockaddr_in host;
  uint adj_PcIdx;
  Bool adj_acknowledged;
  uint8_t shake_step;  ///< Not used yet.
  Bool reply;
  uint32_t n_timeout_skips;
};

struct State
{
  fd_t fd;
  uint32_t enabled;
  uint8_t values[Max_NVariables];
  PcIden pc;
  Channel channels[Max_NChannels];
  FILE* logf;
};

State StateOfThisProcess;


static Bool eq_seq (SeqId a, SeqId b) {
  return a.idx == b.idx && a.key == b.key;
}

#undef BailOut
#define BailOut( ret, msg ) \
do \
{ \
  failmsg(msg); \
  return ret; \
} while (0)

void failmsg(const char* msg)
{
  if (errno != 0) {
    perror(msg);
  }
  else {
    fprintf(stderr, "%s\n", msg);

  }
}

  void
hton_SeqId (SeqId* seq)
{
  seq->idx = htons (seq->idx);
  seq->key = htons (seq->key);
}

  void
ntoh_SeqId (SeqId* seq)
{
  seq->idx = ntohs (seq->idx);
  seq->key = ntohs (seq->key);
}


/** Convert packet data members to network byte order.*/
  void
hton_Packet (Packet* pkt)
{
  hton_SeqId (&pkt->src_seq);
  hton_SeqId (&pkt->dst_seq);
  pkt->enabled = htonl (pkt->enabled);
}

/** Convert packet data members to host byte order.*/
  void
ntoh_Packet (Packet* pkt)
{
  ntoh_SeqId (&pkt->src_seq);
  ntoh_SeqId (&pkt->dst_seq);
  pkt->enabled = ntohl (pkt->enabled);
}

Bool
randomize(void* p, uint size) {
  static uint8_t buf[4096];
  static uint off = sizeof(buf);
  const uint buf_size = sizeof(buf);
  ssize_t nbytes;

  if (size == 0)  return 1;
  if (size + off <= buf_size) {
    memcpy(p, CastOff(void, buf ,+, off), size);
    off += size;
    return 1;
  }
  if (off < buf_size) {
    size -= buf_size - off;
    memcpy(CastOff(void, p ,+, size),
           CastOff(void, buf ,+, off),
           buf_size - off);
  }
  off = 0;

  {
    fd_t urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd < 0)
      BailOut(0, "Failed to open /dev/urandom");

    nbytes = read(urandom_fd, buf, buf_size);
    nbytes += read(urandom_fd, p, size);
    close(urandom_fd);
  }

  if (nbytes != (int)(buf_size+size))
    BailOut(0, "Failed to read from /dev/urandom");

  return 1;
}

  Bool
random_Bool()
{
  uint8_t x = 0;
  Randomize( x );
  return x & 1;
}

/**
 * Update this process's sequence number for a specific channel.
 *
 * A sequence number is a mix between
 * (1) a sequentially increasing number in the low bits and
 * (2) a random number in the high bits.
 */
  void
CMD_seq(Channel* channel)
{
  SeqId* seq = &channel->seq;
  seq->idx += 1;
  Randomize( seq->key );
  channel->adj_acknowledged = 0;
  channel->shake_step = NakShakeStep;
  channel->reply = 1;
}

static
  Bool
channel_idx_ck(PcIden pc, uint i)
{
  return pc.idx != process_of_channel(pc, i);
}

/**
 * Update this process's sequence number for all channels.
 */
  void
CMD_seq_all(State* st)
{
  for (uint i = 0; channel_idx_ck(st->pc, i); ++i) {
    CMD_seq(&st->channels[i]);
  }
}

/**
 * Mark this process as enabled (ready to act) by assigning
 * the {enabled} member of {st}.
 *
 * The {enabled} value can be in three general states:
 * - {enabled == 0}, this process is disabled.
 * - {~enabled == 0}, a flag which has special meaning packets
 * but is invalid for processes.
 * - Anything else signifies an enabled process.
 */
  void
CMD_enable(State* st)
{
  Randomize( st->enabled );
  // Not allowed to be all 0 or 1 bits.
  if (st->enabled == 0 || ~st->enabled == 0)
    st->enabled ^= 1;
  CMD_seq_all(st);
}

/**
 * Mark this process as disabled (not ready to act).
 */
  void
CMD_disable(State* st)
{
  st->enabled = 0;
  CMD_seq_all(st);
}

/**
 * Read a file containing host data of an adjacent process.
 *
 * \sa init_Channel()
 * \sa init_State()
 */
  int
lookup_host(struct sockaddr_in* host, uint id)
{
  int port;
  char hostname[128];
  char fname[128];
  FILE* f;
  int nmatched;
  sprintf(fname, "%s%d", SharedFilePfx, id);
  f = fopen(fname, "rb");
  if (!f) {
    BailOut(-1, "fopen()");
  }
  nmatched = fscanf(f, "%128s %d", hostname, &port);
  if (nmatched < 2)
    BailOut(-1, "fscanf()");
  fclose(f);

  {
    struct hostent* ent;
    ent = gethostbyname(hostname);
    if (!ent)
      BailOut(-1, "gethostbyname()");

    Zeroize( *host );
    host->sin_family = AF_INET;
    memcpy(&host->sin_addr, ent->h_addr, ent->h_length);
    host->sin_port = htons(port);
  }
  return 0;
}

/** Initialize a channel.
 *
 * An important step here is getting host information.
 * \sa lookup_host()
 * \sa init_State()
 */
  void
init_Channel(Channel* channel, State* st)
{
  Zeroize( channel->seq );
  CMD_seq(channel);
  Zeroize( channel->adj_seq );
  Zeroize( channel->host );
  {
    uint id = process_of_channel(st->pc, IdxElt(st->channels, channel));
    channel->adj_PcIdx = id;
    while (0 > lookup_host(&channel->host, id)) {
      usleep(1000 * QuickTimeoutMS);
    }
  }
}

/**
 * Randomly assign all protocol data within reason.
 *
 * Basically this means that we randomize all data which does not
 * define the topology or interact with the operating system.
 * A comprehensive list of all data which is not randomized follows.
 *
 * NOT randomized in State:
 * - {fd}, the socket file descriptor.
 * - {pc.idx}, this process index (convenience).
 * - {pc.npcs}, the number of processes (convenience).
 * - {logf}, pointer to log file.
 * NOT randomized in members of {channels}:
 * - {host}, host information for adjacent process.
 * - {adj_PcIdx}, adjacent process index (convenience).
 * NOT randomized elsewhere:
 * - Program counter.
 * - Operating system data.
 * - Timing variables in main(). The same reasoning holds here as
 *   for not corrupting file descriptors.
 */
  void
randomize_State(State* st)
{
  //State tmp = *st;
  //Randomize( *st );
  Randomize( st->values );
  Randomize( st->enabled );
  for (uint i = 0; channel_idx_ck(st->pc, i); ++i) {
    Channel* channel = &st->channels[i];
    Randomize( channel->seq );
    Randomize( channel->adj_seq );
    channel->adj_acknowledged = random_Bool();
    Randomize( channel->shake_step );
    channel->reply = random_Bool();
    Randomize( channel->n_timeout_skips );
  }
}

/**
 * Initialize the poorly-named State object.
 */
  int
init_State(State* st, uint PcIdx, uint NPcs)
{
  struct sockaddr_in host[1];
  socklen_t sz = sizeof(*host);
  Zeroize( *st );
  st->pc.idx = PcIdx;
  st->pc.npcs = NPcs;

  st->fd = socket(AF_INET, SOCK_DGRAM, 0);
  Zeroize( *host );
  host->sin_family = AF_INET;
  host->sin_addr.s_addr = INADDR_ANY;
  host->sin_port = 0;

#ifdef FixedHostname
  {
    struct hostent* ent;
    ent = gethostbyname(FixedHostname);
    if (!ent)
      BailOut(-1, "gethostbyname()");
    memcpy(&host->sin_addr, ent->h_addr, ent->h_length);
  }
#endif

  if (0 > bind(st->fd, (struct sockaddr *)host, sizeof(*host)))
    BailOut(-1, "bind()");

  if (!UseChecksum)
  {
    const int no_checksum = 1;
    if (0 > setsockopt(st->fd, SOL_SOCKET, SO_NO_CHECK,
                       &no_checksum, sizeof(no_checksum)))
      BailOut(-1, "setsockopt(SO_NO_CHECK)");
  }

  /* Fill in the host address and port.*/
  if (0 > getsockname(st->fd, (struct sockaddr*)host, &sz))
    BailOut(-1, "getsockname()");

  /* Write this process's host info to a file assumed to be shared on NFS.*/
  {
    char hostname[128];
    char fname[128];
    FILE* f;
#ifdef FixedHostname
    strcpy(hostname, FixedHostname);
#else
    if (0 > gethostname(hostname, sizeof(hostname)))
      BailOut(-1, "gethostname()");
#endif
    sprintf(fname, "%s%d", SharedFilePfx, st->pc.idx);
    f = fopen(fname, "wb");
    if (!f)
      BailOut(-1, "fopen()");

    fprintf(f, "%s\n%d", hostname, ntohs(host->sin_port)),
    fclose(f);
  }
  for (uint i = 0; channel_idx_ck(st->pc, i); ++i) {
    init_Channel(&st->channels[i], st);
  }
  randomize_State(st);
  return 0;
}

/**
 * Destroy the poorly-named State object.
 */
  void
lose_State(State* st)
{
  char fname[128];
  sprintf(fname, "%s%d", SharedFilePfx, st->pc.idx);
  remove(fname);
  if (st->logf)
    fclose(st->logf);
}

/** Output a line prefixed by a timestamp and process id.*/
  void
oput_line(const char* line)
{
  char timebuf[200];
  time_t t = time(0);
  struct tm *tmp;
  const State* st = &StateOfThisProcess;
  if (!ShowTimestamps) {
    fprintf(stderr, "%2u %s\n", st->pc.idx, line);
    if (st->logf) {
      fprintf(st->logf, "%2u %s\n", st->pc.idx, line);
      fflush(st->logf);
    }
    return;
  }

  tmp = localtime(&t);
  if (tmp) {
    if (0 == strftime(timebuf, sizeof(timebuf), "%Y.%m.%d %H:%M:%S", tmp)) {
      timebuf[0] = '\0';
    }
  }
  else {
    timebuf[0] = '\0';
  }

  fprintf(stderr, "%s %2u %s\n", timebuf, st->pc.idx, line);
  if (st->logf) {
    fprintf(st->logf, "%s %2u %s\n", timebuf, st->pc.idx, line);
    fflush(st->logf);
  }
}

static
  void
cstr_of_values(char* s, const uint8_t* values, PcIden pc, uint channel_idx, Bool writing)
{
  uint off = 0;
  s[0] = '\0';
  for (uint i = 0; i < Max_NVariables; ++i) {
    uint8_t val;
    uint vbl_idx;
    uint domsz;
    if (channel_idx == Max_NChannels)
      vbl_idx = i;
    else
      vbl_idx = variable_of_channel(pc, channel_idx, i, writing);

    domsz = domsz_of_variable(pc, vbl_idx);
    if (domsz == 0)  return;

    if (!writing)
      val = values[i];
    else
      val = values[vbl_idx];

    val %= domsz;

    if (i > 0)  s[off++] = ' ';
    off += sprintf(&s[off], "%u", (uint)val);
  }
}


/**
 * Send a packet.
 */
  int
send_Packet (const Packet* packet, const struct sockaddr_in* host, State* st)
{
  Packet pkt[1];
  int stat;
  *pkt = *packet;
  if (ShowCommunication) {
    char local_vals_buf[100];
    char channel_vals_buf[100];
    char buf[1024];
    uint channel_idx = Max_NChannels;
    for (uint i = 0; channel_idx_ck(st->pc, i); ++i) {
      if (&st->channels[i].host == host) {
        channel_idx = i;
        break;
      }
    }
    cstr_of_values(local_vals_buf, st->values, st->pc, Max_NChannels, 0);
    cstr_of_values(channel_vals_buf, st->values, st->pc, channel_idx, 1);
    sprintf(buf, "-> %2u  (%s)  src/dst idx:%04hx/%04hx  key:%04hx/%04hx  enabled:%08x  values:(%s)",
            process_of_channel(st->pc, channel_idx), local_vals_buf,
            pkt->src_seq.idx, pkt->dst_seq.idx,
            pkt->src_seq.key, pkt->dst_seq.key,
            pkt->enabled, channel_vals_buf);
    oput_line(buf);
  }
  hton_Packet(pkt);

  if (NetworkReliability < 1) {
    uint32_t x = 0;
    Randomize( x );
    if (NetworkReliability * ~(uint32_t)0 < x) {
      // Packet dropped.
      return sizeof(*pkt);
    }
  }

  stat = sendto(st->fd, pkt, sizeof(*pkt), 0,
                (struct sockaddr*)host, sizeof(*host));
  return stat;
}

/**
 * Receive a packet and figure out which neighbor sent it.
 */
  Channel*
recv_Packet (Packet* pkt, State* st)
{
  int cc;
  struct sockaddr_in host[1];
  socklen_t sz = sizeof(*host);
  Zeroize( *host );
  cc = recvfrom(st->fd, pkt, sizeof(*pkt), 0, (struct sockaddr*)host, &sz);
  if (cc != sizeof(*pkt)) {
    return 0;
    BailOut(0, "recvfrom()");
  }
  ntoh_Packet(pkt);

  for (uint i = 0; channel_idx_ck(st->pc, i); ++i) {
    Channel* channel = &st->channels[i];
    if (channel->host.sin_addr.s_addr == host->sin_addr.s_addr &&
        channel->host.sin_port == host->sin_port)
    {
      if (ShowCommunication) {
        char local_vals_buf[100];
        char channel_vals_buf[100];
        char buf[1024];
        cstr_of_values(local_vals_buf, st->values, st->pc, Max_NChannels, 0);
        cstr_of_values(channel_vals_buf, pkt->values, st->pc, i, 0);
        sprintf(buf, "<- %2u  (%s)  src/dst idx:%04hx/%04hx  key:%04hx/%04hx  enabled:%08x  values:(%s)",
                channel->adj_PcIdx, local_vals_buf,
                pkt->src_seq.idx, pkt->dst_seq.idx,
                pkt->src_seq.key, pkt->dst_seq.key,
                pkt->enabled, channel_vals_buf);
        oput_line(buf);
      }
      return channel;
    }
  }

  return 0;
  //BailOut(0, "who sent the message?");
}

/**
 * Perform an action or check to see if an action can occur.
 * If {modify} is true, then the process state will be modified.
 *
 * \return Whether the process can or did act.
 */
  Bool
CMD_act(State* st, Bool modify)
{
  uint8_t values[Max_NVariables];

  for (uint i = 0; i < Max_NVariables; ++i)
  {
    const uint domsz = domsz_of_variable(st->pc, i);
    if (domsz == 0)  break;
    if (st->values[i] >= domsz)
      st->values[i] %= domsz;
  }
  memcpy(values, st->values, sizeof(values));

  action_assign(st->pc, values);

  if (0 != memcmp(values, st->values, sizeof(values))) {
    if (modify) {

      action_pre_assign(st->pc, st->values);

      if (ActionLagMS > 0)
      {
        // Lag actions a bit to expose livelocks.
        uint32_t x = 0;
        Randomize( x );
        //usleep(1000 * ActionLagMS);
        usleep(1000 * (ActionLagMS/2 + x % ActionLagMS));
      }

      memcpy(st->values, values, sizeof(values));
    }
    return 1;
  }
  return 0;
}

/**
 * Send the current process state to some neighbor
 * using a custom {enabled} value.
 */
  void
CMD_send1(Channel* channel, State* st, uint32_t enabled)
{
  Packet pkt[1];
  pkt->src_seq = channel->seq;
  pkt->dst_seq = channel->adj_seq;
  pkt->enabled = enabled;
  Zeroize( pkt->values );
  for (uint i = 0; i < Max_NVariables; ++i) {
    const uint vbl_idx =
      variable_of_channel(st->pc, IdxElt(st->channels, channel), i, 1);
    if (vbl_idx >= Max_NVariables)  break;
    pkt->values[i] = st->values[vbl_idx];
  }
  channel->n_timeout_skips = NQuickTimeouts-1;
  channel->reply = 0;
  send_Packet(pkt, &channel->host, st);
}

/**
 * Send the current process state to some neighbor.
 */
  void
CMD_send(Channel* channel, State* st)
{
  CMD_send1(channel, st, st->enabled);
}

/**
 * Broadcast to all neighbors.
 * (More or less... some are excluded for efficiency.)
 */
  void
CMD_send_all(State* st)
{
  for (uint i = 0; channel_idx_ck(st->pc, i); ++i) {
    Channel* channel = &st->channels[i];
    if (st->enabled != 0 && channel->adj_acknowledged)
      continue;
    CMD_send(channel, st);
  }
}

/**
 * Check if this process should be enabled or disabled.
 * Change {st->enabled} accordingly.
 */
  Bool
update_enabled(State* st)
{
  // Sanitize this value.
  if (~st->enabled == 0)
    st->enabled = 0;

  if (CMD_act(st, 0)) {
    if (st->enabled == 0) {
      CMD_enable(st);
      return 1;
    }
  }
  else {
    if (st->enabled != 0) {
      CMD_disable(st);
      return 1;
    }
  }
  return 0;
}


/**
 * TODO: This comment is not completely accurate.
 *
 * - Each variable is owned by a single process.
 * - In each packet sent to another process, include the current
 * values of all the variables it can read (that the sending process owns).
 *
 * States:
 *   0. Disabled
 *   1. Requesting
 *
 * Things I can do:
 * - ACT: Perform an enabled action based on currently known values.
 *   \sa CMD_act()
 * - SEQ: Randomly assign/increment {src_seq} to a new value.
 *   \sa CMD_seq()
 * - SEQ_ALL: Call SEQ for all channels.
 *   \sa CMD_seq_all()
 * - DISABLE: Assign {enabled} to zero. Also call SEQ.
 *   \sa CMD_disable()
 * - ENABLE: Randomly assign {enabled} to some positive value. Also call SEQ.
 *   \sa CMD_enable()
 * - SEND: Send info.
 *   \sa CMD_send()
 * - SEND_ALL: Send info to all.
 *   \sa CMD_send_all()
 *
 *
 * Case: Both disabled.
 * # If I receive a message which has the wrong sequence number for me,
 * then SEND using my sequence number as {src_seq}
 * # If I receieve a message which uses my correct sequence number,
 * but I don't recognize the other's sequence number,
 * then SEND.
 * # If I don't receive a message after some timeout,
 * then SEND.
 *
 * Case: I am disabled, neighbor is enabled to act.
 * # If I get a message with a positive {enabled} value,
 * then SEQ, SEND.
 *
 * Case: I am enabled to act.
 * # ENABLE
 * # If all reply using the new src_seq number and lower enabled values,
 * then ACT, DISABLE, SEND.
 * # If one of the replies has an {enabled} value greater than my own,
 * then SEND. Don't do anything else.
 * # If all reply using the new src_seq number and lower or
 * equal enabled values (including equal values),
 * then ENABLE, SEND.
 * # If some message contains new values which disable all of my actions,
 * then DISABLE, SEND.
 */
  int
handle_recv (Packet* pkt, Channel* channel, State* st)
{
  Bool bcast = 0;
  Bool reply = 0;
  Bool adj_acted = 0;

  // If the packet doesn't know this process's sequence number,
  // then reply with the current data.
  if (!eq_seq (pkt->dst_seq, channel->seq)) {
    if (eq_seq (pkt->src_seq, channel->adj_seq) && !channel->adj_acknowledged) {
      // 50% chance of not replying.
      if (!channel->reply) {
        channel->reply = 1;
        return 0;
      }
    }
    pkt->dst_seq = pkt->src_seq;
    pkt->src_seq = channel->seq;
    if (st->enabled != 0 && st->enabled < pkt->enabled) {
      pkt->enabled = 0;
      pkt->enabled = ~pkt->enabled;
    }
    else {
      pkt->enabled = st->enabled;
    }
    for (uint i = 0; i < Max_NVariables; ++i) {
      const uint vbl_idx =
        variable_of_channel(st->pc, IdxElt(st->channels, channel), i, 1);
      if (vbl_idx >= Max_NVariables)  break;
      pkt->values[i] = st->values[vbl_idx];
    }
    channel->reply = 0;
    send_Packet(pkt, &channel->host, st);
    return 1;
  }

  // Update the current values of the adjacent process's states.
  for (uint i = 0; i < Max_NVariables; ++i) {
    const uint vbl_idx = variable_of_channel(st->pc, IdxElt(st->channels, channel), i, 0);
    if (vbl_idx >= Max_NVariables)
      break;
    if (st->values[vbl_idx] != pkt->values[i]) {
      st->values[vbl_idx] = pkt->values[i];
      adj_acted = 1;
    }
  }

  if (st->enabled != 0) {
    // This process is enabled and waiting to act.
    // It has updated its sequence number already
    // and has sent its intent to act to the adjacent processes.
    if (pkt->enabled < st->enabled) {
      // The adjacent process is disabled or has a lower priority than this process.
      // It knows our current sequence number,
      // so count that as an acknowledgment that this process can act.
      channel->adj_acknowledged = 1;
    }
  }

  // If this process just became enabled or disabled,
  // then update the sequence number and prepare to broadcast
  // to all adjacent processes.
  if (update_enabled(st)) {
    bcast = 1;
  }
  else if (adj_acted && st->enabled == 0) {
    CMD_seq(channel);
    channel->adj_acknowledged = 1;
    reply = 1;
  }

  if (!eq_seq (channel->adj_seq, pkt->src_seq)) {
    // The adjacent process updated its sequence number.
    channel->adj_seq = pkt->src_seq;
    // If it just became enabled and this process is disabled,
    // update this process's sequence number if it hasn't already.
    if (pkt->enabled != 0 && ~pkt->enabled != 0 && st->enabled == 0) {
      if (eq_seq (pkt->dst_seq, channel->seq)) {
        CMD_seq(channel);
      }
    }
  }

  if (~pkt->enabled == 0) {
    // We always want to reply in this case.
    reply = 1;
  }
  else if (pkt->enabled != 0) {
    if (!channel->adj_acknowledged) {
      reply = 1;
    }
  }

  if (st->enabled != 0) {
    // This process is enabled.
    if (pkt->enabled == st->enabled) {
      // The adjacent process has the same priority to act!
      // Update our priority and sequence number.
      // All adjacent processes must acknowledge before we can act.
      CMD_enable(st);
      bcast = 1;
    }
    else if (pkt->enabled > st->enabled) {
      if (~pkt->enabled != 0) {
        CMD_seq(channel);
      }
    }

    if (~pkt->enabled == 0) {
      // We always want to reply in this case.
    }
    else if (channel->adj_acknowledged) {
      // No need to reply when the adjacent process already
      // acknowledged that this process can act.
      reply = 0;
    }
  }
  else if (eq_seq (pkt->dst_seq, channel->seq)) {
    channel->adj_acknowledged = 1;
    if (!reply) {
      // 50% chance of not replying.
      if (channel->reply) {
        reply = 1;
        // {channel->reply} is assigned 0 in CMD_send()
        // {channel->n_timeout_skips} is assigned {NQuickTimeouts-1} in CMD_send()
      }
      else {
        channel->reply = 1;
      }
    }
  }

  if (st->enabled != 0)
  {
    Bool acknowledged = 1;
    for (uint i = 0; channel_idx_ck(st->pc, i); ++i) {
      if (Max_NVariables <= variable_of_channel(st->pc, i, 0, 0))
        continue;
      if (!st->channels[i].adj_acknowledged)
        acknowledged  = 0;
    }
    if (acknowledged) {
      // This process is enabled and all adjacent processes
      // have acknowledged that it can act!
      while (CMD_act(st, 1)) {
        // Keep acting until disabled.
      }
      CMD_disable(st);
      bcast = 1;
    }
  }

  if (bcast) {
    // We are broadcasting to all adjacent processes.
    CMD_send_all(st);
    // Return that a broadcast occurred.
    return 2;
  }
  else if (reply) {
    // Just reply to the sending process.
    CMD_send(channel, st);
    // Return that a reply occurred.
    return 1;
  }

  return 0;
}

  void
handle_timeout (State* st)
{
  const uint skip_limit = NQuickTimeouts/2 + NQuickTimeouts;
  update_enabled(st);
  for (uint i = 0; channel_idx_ck(st->pc, i); ++i) {
    Channel* channel = &st->channels[i];
    if (channel->n_timeout_skips >= skip_limit) {
      channel->n_timeout_skips = NQuickTimeouts-1;
    }
    if (st->enabled != 0 && channel->adj_acknowledged)
      continue;
    if (st->enabled == 0 && channel->adj_acknowledged) {
      if (channel->n_timeout_skips > 0) {
        channel->n_timeout_skips -= 1;
        continue;
      }
      channel->adj_acknowledged = 0;
      CMD_send1(channel, st, ~(uint32_t)0);
    }
    else {
      CMD_send(channel, st);
    }
    {
      uint32_t x = 0;
      Randomize( x );
      channel->n_timeout_skips = NQuickTimeouts / 2 + x % NQuickTimeouts;
    }
  }
}

static Bool terminating = 0;
static void set_term_flag()
{
  terminating = 1;
}

static void randomize_process_state()
{
  randomize_State(&StateOfThisProcess);
}


static int
init_timeout(timer_t* timeout_id)
{
  struct sigevent timeout_sigevent[1];
  int stat = 0;
  Zeroize( *timeout_sigevent );
  timeout_sigevent->sigev_notify = SIGEV_NONE;
  stat = timer_create(CLOCK_REALTIME, timeout_sigevent, timeout_id);
  if (stat != 0)
    BailOut(stat, "timer_create()");
  return 0;
}

static int
reset_timeout(timer_t timeout_id, uint timeout_ms) {
  struct itimerspec timeout_spec[1];
  int stat = 0;
  Zeroize( *timeout_spec );
  timeout_spec->it_value.tv_sec = timeout_ms / 1000;
  timeout_spec->it_value.tv_nsec = 1000000 * (timeout_ms % 1000);
  stat = timer_settime(timeout_id, 0, timeout_spec, 0);
  if (stat != 0)
    BailOut(stat, "timer_settime()");
  return 0;
}

int main(int argc, char** argv)
{
  State* st = &StateOfThisProcess;
  int argi = 1;
  timer_t timeout_id;
  uint timeout_ms = 0;
  (void) argc;

  /* remove("shared-resource"); */

  // The two common kill signals try to cleanly exit.
  signal(SIGTERM, set_term_flag);
  signal(SIGINT, set_term_flag);
  // The first user signal randomizes all protocol data!
  signal(SIGUSR1, randomize_process_state);

  {
    uint PcIdx;
    uint NPcs;
    if (!argv[argi] || 1 != sscanf(argv[argi], "%u", &PcIdx))
      BailOut(1, "First argument must be an unsigned int.");
    argi += 1;

#ifdef NProcesses
    NPcs = NProcesses;
#else
    if (!argv[argi] || 1 != sscanf(argv[argi], "%u", &NPcs) || NPcs == 0)
      BailOut(1, "Second argument must be a positive unsigned int.");
    argi += 1;
#endif
    init_State(st, PcIdx, NPcs);
  }

  if (argv[argi] && 0 == strcmp("-o-log", argv[argi])) {
    argi += 1;
    if (!argv[argi])
      BailOut(1, "-o-log must be followed by a file name.");
    st->logf = fopen(argv[argi], "wb");
    argi += 1;
  }

  init_timeout(&timeout_id);


  while (!terminating)
  {
    Packet pkt[1];
    int stat;
    struct pollfd pfd[1];

    pfd->fd = st->fd;
    pfd->events = POLLIN;
    pfd->revents = 0;
    stat = poll(pfd, 1, timeout_ms);

    if (terminating)
      break;

    if (stat == 0) {
      // We hit timeout, resend things.
      handle_timeout(st);
      reset_timeout(timeout_id, QuickTimeoutMS);
    }
    else if (stat == 1) {
      // Ok got message.
      Channel* channel = recv_Packet(pkt, st);
      if (channel) {
        if (2 == handle_recv(pkt, channel, st)) {
          reset_timeout(timeout_id, QuickTimeoutMS);
        }
      }
    }
    else {
      // Handle error
      failmsg("Some error in poll()");
    }

    {
      struct itimerspec timeout_info[1];
      if (0 == timer_gettime(timeout_id, timeout_info)) {
        timeout_ms = timeout_info->it_value.tv_sec * 1000;
        timeout_ms += timeout_info->it_value.tv_nsec / 1000000;
      }
    }
  }
  timer_delete(timeout_id);
  lose_State(st);
  return 0;
}

