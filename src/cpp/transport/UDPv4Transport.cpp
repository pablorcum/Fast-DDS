#include <fastrtps/transport/UDPv4Transport.h>
#include <utility>
#include <cstring>
#include <algorithm>
#include <fastrtps/utils/IPFinder.h>
#include <fastrtps/utils/RTPSLog.h>

using namespace std;
using namespace boost::asio;
using namespace boost::interprocess;

namespace eprosima{
namespace fastrtps{
namespace rtps{

static const uint32_t maximumUDPSocketSize = 65536;
static const char* const CLASS_NAME = "UDPv4Transport";

UDPv4Transport::UDPv4Transport(const TransportDescriptor& descriptor):
   mSendBufferSize(descriptor.sendBufferSize),
   mReceiveBufferSize(descriptor.receiveBufferSize),
   mGranularMode(descriptor.granularMode),
   mInterfaceWhiteList(descriptor.interfaceWhiteList)
{
   auto ioServiceFunction = [&]()
   {
      io_service::work work(mService);
      mService.run();
   };
   ioServiceThread.reset(new boost::thread(ioServiceFunction));
}

UDPv4Transport::TransportDescriptor::TransportDescriptor():
   sendBufferSize(maximumUDPSocketSize),
   receiveBufferSize(maximumUDPSocketSize),
   granularMode(false)
{}

UDPv4Transport::UDPv4Transport()
{
   auto ioServiceFunction = [&]()
   {
      io_service::work work(mService);
      mService.run();
   };
   ioServiceThread.reset(new boost::thread(ioServiceFunction));
}

UDPv4Transport::~UDPv4Transport()
{
   mService.stop();
   ioServiceThread->join();
}

bool UDPv4Transport::IsInputChannelOpen(const Locator_t& locator) const
{
   boost::unique_lock<boost::recursive_mutex> scopedLock(mInputMapMutex);
   return IsLocatorSupported(locator) && (mInputSockets.find(locator.port) != mInputSockets.end());
}

bool UDPv4Transport::IsOutputChannelOpen(const Locator_t& locator) const
{
   boost::unique_lock<boost::recursive_mutex> scopedLock(mOutputMapMutex);
   if (!IsLocatorSupported(locator))
      return false;

   if (mGranularMode)
      return mGranularOutputSockets.find(locator) != mGranularOutputSockets.end();
   else 
      return mOutputSockets.find(locator.port) != mOutputSockets.end();
}

bool UDPv4Transport::OpenOutputChannel(const Locator_t& locator)
{
   if (IsOutputChannelOpen(locator) ||
       !IsLocatorSupported(locator))
      return false;   
  
   if (mGranularMode)   
      return OpenAndBindGranularOutputSocket(locator);
   else
      return OpenAndBindOutputSockets(locator.port);
}

static bool IsMulticastAddress(const Locator_t& locator)
{
   return locator.address[12] >= 224 && locator.address[12] <= 239;
}

bool UDPv4Transport::OpenInputChannel(const Locator_t& locator)
{
   if (!IsLocatorSupported(locator))
      return false;   

   bool success = false;

   if (!IsInputChannelOpen(locator))
      success = OpenAndBindInputSockets(locator.port);

   if (IsMulticastAddress(locator))
   {
      // The multicast group will be joined silently, because we do not
      // want to return another resource.
      auto& socket = mInputSockets.at(locator.port);
      socket.set_option(ip::multicast::join_group(ip::address_v4::from_string(locator.to_IP4_string())));
   }

   return success;
}

bool UDPv4Transport::CloseOutputChannel(const Locator_t& locator)
{
   if (!IsOutputChannelOpen(locator))
      return false;   

   boost::unique_lock<boost::recursive_mutex> scopedLock(mOutputMapMutex);
   if (mGranularMode)
   {
      auto& socket = mGranularOutputSockets.at(locator);
      socket.cancel();
      socket.close();
      mGranularOutputSockets.erase(locator);
   }
   else
   {
      auto& sockets = mOutputSockets.at(locator.port);
      for (auto& socket : sockets)
      {
         socket.cancel();
         socket.close();
      }

      mOutputSockets.erase(locator.port);
   }

   return true;
}

bool UDPv4Transport::CloseInputChannel(const Locator_t& locator)
{
   if (!IsInputChannelOpen(locator))
      return false;   

   boost::unique_lock<boost::recursive_mutex> scopedLock(mInputMapMutex);
  
   auto& socket = mInputSockets.at(locator.port);
   socket.cancel();
   socket.close();

   mInputSockets.erase(locator.port);
   return true;
}

static void GetIP4s(vector<IPFinder::info_IP>& locNames)
{
   IPFinder::getIPs(&locNames);
   auto newEnd = remove_if(locNames.begin(), 
                 locNames.end(),
                 [](IPFinder::info_IP ip){return ip.type != IPFinder::IP4;});
   locNames.erase(newEnd, locNames.end());
}

bool UDPv4Transport::IsInterfaceAllowed(const ip::address_v4& ip)
{
   if (mInterfaceWhiteList.empty())
      return true;
   
   if (ip == ip::address_v4::any())
      return true;

   return  find(mInterfaceWhiteList.begin(), mInterfaceWhiteList.end(), ip) != mInterfaceWhiteList.end();
}

bool UDPv4Transport::OpenAndBindOutputSockets(uint16_t port)
{
	const char* const METHOD_NAME = "OpenAndBindOutputSockets";

   boost::unique_lock<boost::recursive_mutex> scopedLock(mOutputMapMutex);

   try 
   {
      // If there is no whitelist, we can simply open a generic output socket
      // and gain efficiency.
      if (mInterfaceWhiteList.empty())
         mOutputSockets[port].push_back(OpenAndBindUnicastOutputSocket(ip::address_v4::any(), port));
      else
      {
         std::vector<IPFinder::info_IP> locNames;
         GetIP4s(locNames);
         for (const auto& infoIP : locNames)
         {
            auto ip = boost::asio::ip::address_v4::from_string(infoIP.name);
            if (IsInterfaceAllowed(ip))
               mOutputSockets[port].push_back(OpenAndBindUnicastOutputSocket(ip, port));
         }
      }

   }
	catch (boost::system::system_error const& e)
   {
	   logInfo(RTPS_MSG_OUT, "UDPv4 Error binding at port: (" << port << ")" << " with boost msg: "<<e.what() , C_YELLOW);
      mOutputSockets.erase(port);
      return false;
   }

   return true;
}

bool UDPv4Transport::OpenAndBindGranularOutputSocket(const Locator_t& locator)
{
	const char* const METHOD_NAME = "OpenAndBindGranularOutputSocket";
   auto ip = boost::asio::ip::address_v4::from_string(locator.to_IP4_string());
   if (!IsInterfaceAllowed(ip))
      return false;

   boost::unique_lock<boost::recursive_mutex> scopedLock(mOutputMapMutex);

   try 
   {
      mGranularOutputSockets.insert(std::pair<Locator_t, boost::asio::ip::udp::socket>(locator, 
         OpenAndBindUnicastOutputSocket(boost::asio::ip::address_v4::from_string(locator.to_IP4_string()), locator.port)));
   }
	catch (boost::system::system_error const& e)
   {
	   logInfo(RTPS_MSG_OUT, "UDPv4 Error binding at port: (" << locator.port << ")" << " with boost msg: "<<e.what() , C_YELLOW);
      mGranularOutputSockets.erase(locator);
      return false;
   }

   return true;
}

bool UDPv4Transport::OpenAndBindInputSockets(uint16_t port)
{
	const char* const METHOD_NAME = "OpenAndBindInputSockets";
   
   boost::unique_lock<boost::recursive_mutex> scopedLock(mInputMapMutex);

   try 
   {
      mInputSockets.emplace(port, OpenAndBindInputSocket(port));
   }
	catch (boost::system::system_error const& e)
   {
	   logInfo(RTPS_MSG_OUT, "UDPv4 Error binding at port: (" << port << ")" << " with boost msg: "<<e.what() , C_YELLOW);
      mInputSockets.erase(port);
      return false;
   }

   return true;
}

boost::asio::ip::udp::socket UDPv4Transport::OpenAndBindUnicastOutputSocket(const ip::address_v4& ipAddress, uint32_t port)
{
   ip::udp::socket socket(mService);
   socket.open(ip::udp::v4());
   socket.set_option(socket_base::send_buffer_size(mSendBufferSize));

   ip::udp::endpoint endpoint(ipAddress, port);
   socket.bind(endpoint);

   return socket;
}

boost::asio::ip::udp::socket UDPv4Transport::OpenAndBindInputSocket(uint32_t port)
{
   ip::udp::socket socket(mService);
   socket.open(ip::udp::v4());
   socket.set_option(socket_base::receive_buffer_size(mReceiveBufferSize));
	socket.set_option(ip::udp::socket::reuse_address( true ) );
	socket.set_option(ip::multicast::enable_loopback( true ) );
   auto anyIP = ip::address_v4::any();

   ip::udp::endpoint endpoint(anyIP, port);
   socket.bind(endpoint);

   return socket;
}

bool UDPv4Transport::DoLocatorsMatch(const Locator_t& left, const Locator_t& right) const
{
   if (mGranularMode)
      return left == right;
   else
      return left.port == right.port;
}

bool UDPv4Transport::IsLocatorSupported(const Locator_t& locator) const
{
   return locator.kind == LOCATOR_KIND_UDPv4;
}

Locator_t UDPv4Transport::RemoteToMainLocal(const Locator_t& remote) const
{
   if (!IsLocatorSupported(remote))
      return false;

   Locator_t mainLocal(remote);
   memset(mainLocal.address, 0x00, sizeof(mainLocal.address));
   return mainLocal;
}

bool UDPv4Transport::Send(const octet* sendBuffer, uint32_t sendBufferSize, const Locator_t& localLocator, const Locator_t& remoteLocator)
{
   if (!IsOutputChannelOpen(localLocator) ||
       sendBufferSize > mSendBufferSize)
      return false;
      
   boost::unique_lock<boost::recursive_mutex> scopedLock(mOutputMapMutex);
   bool success = false;

   if (mGranularMode)
   {
      auto& socket = mGranularOutputSockets.at(localLocator);
      success |= SendThroughSocket(sendBuffer, sendBufferSize, remoteLocator, socket);
   }
   else
   {
      auto& sockets = mOutputSockets.at(localLocator.port);
      for (auto& socket : sockets)
         success |= SendThroughSocket(sendBuffer, sendBufferSize, remoteLocator, socket);
   }

   return success;
}

static void EndpointToLocator(ip::udp::endpoint& endpoint, Locator_t& locator)
{
   locator.port = endpoint.port();
   auto ipBytes = endpoint.address().to_v4().to_bytes();
   memcpy(&locator.address[12], ipBytes.data(), sizeof(ipBytes));
}

bool UDPv4Transport::Receive(std::vector<octet>& receiveBuffer, const Locator_t& localLocator, Locator_t& remoteLocator)
{
	const char* const METHOD_NAME = "Receive";
   if (!IsInputChannelOpen(localLocator) ||
       receiveBuffer.capacity() < mReceiveBufferSize)
      return false;

   receiveBuffer.resize(mReceiveBufferSize);

   interprocess_semaphore receiveSemaphore(0);
   bool success = false;

   auto handler = [&receiveBuffer, &success, METHOD_NAME, &receiveSemaphore](const boost::system::error_code& error, std::size_t bytes_transferred)
   {
	   if(error != boost::system::errc::success)
      {
		   logInfo(RTPS_MSG_IN, "Error while listening to socket...",C_BLUE);
         receiveBuffer.resize(0);
      }
      else 
      {
		   logInfo(RTPS_MSG_IN,"Msg processed (" << bytes_transferred << " bytes received), Socket async receive put again to listen ",C_BLUE);
         receiveBuffer.resize(bytes_transferred);
         success = true;
      }
      
      receiveSemaphore.post();
   };

   ip::udp::endpoint senderEndpoint;
   
   { // lock scope
      boost::unique_lock<boost::recursive_mutex> scopedLock(mInputMapMutex);
      if (!IsInputChannelOpen(localLocator))
         return false;

      auto& socket = mInputSockets.at(localLocator.port);
      socket.async_receive_from(boost::asio::buffer(receiveBuffer),
                                senderEndpoint,
                                handler);
   }

   receiveSemaphore.wait();
   if (success)
      EndpointToLocator(senderEndpoint, remoteLocator);

   return success;
}

bool UDPv4Transport::SendThroughSocket(const octet* sendBuffer,
                                       uint32_t sendBufferSize,
                                       const Locator_t& remoteLocator,
                                       boost::asio::ip::udp::socket& socket)
{
	const char* const METHOD_NAME = "SendThroughSocket";

	boost::asio::ip::address_v4::bytes_type remoteAddress;
   memcpy(&remoteAddress, &remoteLocator.address[12], sizeof(remoteAddress));
   auto destinationEndpoint = ip::udp::endpoint(boost::asio::ip::address_v4(remoteAddress), static_cast<uint16_t>(remoteLocator.port));
   unsigned int bytesSent = 0;
   logInfo(RTPS_MSG_OUT,"UDPv4: " << sendBufferSize << " bytes TO endpoint: " << destinationEndpoint
         << " FROM " << socket.local_endpoint(), C_YELLOW);

   try 
   {
      bytesSent = socket.send_to(boost::asio::buffer(sendBuffer, sendBufferSize), destinationEndpoint);
   }
   catch (const std::exception& error) 
   {
      logWarning(RTPS_MSG_OUT, "Error: " << error.what(), C_YELLOW);
      return false;
   }

   (void) bytesSent;
	logInfo (RTPS_MSG_OUT,"SENT " << bytesSent,C_YELLOW);
   return true;
}

} // namespace rtps
} // namespace fastrtps
} // namespace eprosima
